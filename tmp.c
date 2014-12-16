 1 /*
  2     Copyright (C) 2001 Paul Davis
  3     Copyright (C) 2003 Jack O'Quin
  4     
  5     This program is free software; you can redistribute it and/or modify
  6     it under the terms of the GNU General Public License as published by
  7     the Free Software Foundation; either version 2 of the License, or
  8     (at your option) any later version.
  9 
 10     This program is distributed in the hope that it will be useful,
 11     but WITHOUT ANY WARRANTY; without even the implied warranty of
 12     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 13     GNU General Public License for more details.
 14 
 15     You should have received a copy of the GNU General Public License
 16     along with this program; if not, write to the Free Software
 17     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 18 
 19     * 2002/08/23 - modify for libsndfile 1.0.0 <andy@alsaplayer.org>
 20     * 2003/05/26 - use ringbuffers - joq
 21     
 22     $Id: capture_client.c,v 1.9 2003/10/31 20:52:23 joq Exp $
 23 */
 24 
 25 #include <stdio.h>
 26 #include <stdlib.h>
 27 #include <string.h>
 28 #include <errno.h>
 29 #include <unistd.h>
 30 #include <sndfile.h>
 31 #include <pthread.h>
 32 #include <getopt.h>
 33 #include <jack/jack.h>
 34 #include <jack/ringbuffer.h>
 35 
 36 typedef struct _thread_info {
 37     pthread_t thread_id;
 38     SNDFILE *sf;
 39     jack_nframes_t duration;
 40     jack_nframes_t rb_size;
 41     jack_client_t *client;
 42     unsigned int channels;
 43     int bitdepth;
 44     char *path;
 45     volatile int can_capture;
 46     volatile int can_process;
 47     volatile int status;
 48 } thread_info_t;
 49 
 50 /* JACK data */
 51 unsigned int nports;
 52 jack_port_t **ports;
 53 jack_default_audio_sample_t **in;
 54 jack_nframes_t nframes;
 55 const size_t sample_size = sizeof(jack_default_audio_sample_t);
 56 
 57 /* Synchronization between process thread and disk thread. */
 58 #define DEFAULT_RB_SIZE 16384           /* ringbuffer size in frames */
 59 jack_ringbuffer_t *rb;
 60 pthread_mutex_t disk_thread_lock = PTHREAD_MUTEX_INITIALIZER;
 61 pthread_cond_t  data_ready = PTHREAD_COND_INITIALIZER;
 62 long overruns = 0;
 63 
 64 
 65 void *
 66 disk_thread (void *arg)
 67 {
 68         thread_info_t *info = (thread_info_t *) arg;
 69         static jack_nframes_t total_captured = 0;
 70         jack_nframes_t samples_per_frame = info->channels;
 71         size_t bytes_per_frame = samples_per_frame * sample_size;
 72         void *framebuf = malloc (bytes_per_frame);
 73 
 74         pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
 75         pthread_mutex_lock (&disk_thread_lock);
 76 
 77         info->status = 0;
 78 
 79         while (1) {
 80 
 81                 /* Write the data one frame at a time.  This is
 82                  * inefficient, but makes things simpler. */
 83                 while (info->can_capture &&
 84                        (jack_ringbuffer_read_space (rb) >= bytes_per_frame)) {
 85 
 86                         jack_ringbuffer_read (rb, framebuf, bytes_per_frame);
 87 
 88                         if (sf_writef_float (info->sf, framebuf, 1) != 1) {
 89                                 char errstr[256];
 90                                 sf_error_str (0, errstr, sizeof (errstr) - 1);
 91                                 fprintf (stderr,
 92                                          "cannot write sndfile (%s)\n",
 93                                          errstr);
 94                                 info->status = EIO; /* write failed */
 95                                 goto done;
 96                         }
 97                                 
 98                         if (++total_captured >= info->duration) {
 99                                 printf ("disk thread finished\n");
100                                 goto done;
101                         }
102                 }
103 
104                 /* wait until process() signals more data */
105                 pthread_cond_wait (&data_ready, &disk_thread_lock);
106         }
107 
108  done:
109         pthread_mutex_unlock (&disk_thread_lock);
110         free (framebuf);
111         return 0;
112 }
113         
114 int
115 process (jack_nframes_t nframes, void *arg)
116 {
117         int chn;
118         size_t i;
119         thread_info_t *info = (thread_info_t *) arg;
120 
121         /* Do nothing until we're ready to begin. */
122         if ((!info->can_process) || (!info->can_capture))
123                 return 0;
124 
125         for (chn = 0; chn < nports; chn++)
126                 in[chn] = jack_port_get_buffer (ports[chn], nframes);
127 
128         /* Sndfile requires interleaved data.  It is simpler here to
129          * just queue interleaved samples to a single ringbuffer. */
130         for (i = 0; i < nframes; i++) {
131                 for (chn = 0; chn < nports; chn++) {
132                         if (jack_ringbuffer_write (rb, (void *) (in[chn]+i),
133                                               sample_size)
134                             < sample_size)
135                                 overruns++;
136                 }
137         }
138 
139         /* Tell the disk thread there is work to do.  If it is already
140          * running, the lock will not be available.  We can't wait
141          * here in the process() thread, but we don't need to signal
142          * in that case, because the disk thread will read all the
143          * data queued before waiting again. */
144         if (pthread_mutex_trylock (&disk_thread_lock) == 0) {
145             pthread_cond_signal (&data_ready);
146             pthread_mutex_unlock (&disk_thread_lock);
147         }
148 
149         return 0;
150 }
151 
152 void
153 jack_shutdown (void *arg)
154 {
155         fprintf (stderr, "JACK shutdown\n");
156         // exit (0);
157         abort();
158 }
159 
160 void
161 setup_disk_thread (thread_info_t *info)
162 {
163         SF_INFO sf_info;
164         int short_mask;
165         
166         sf_info.samplerate = jack_get_sample_rate (info->client);
167         sf_info.channels = info->channels;
168         
169         switch (info->bitdepth) {
170                 case 8: short_mask = SF_FORMAT_PCM_U8;
171                         break;
172                 case 16: short_mask = SF_FORMAT_PCM_16;
173                          break;
174                 case 24: short_mask = SF_FORMAT_PCM_24;
175                          break;
176                 case 32: short_mask = SF_FORMAT_PCM_32;
177                          break;
178                 default: short_mask = SF_FORMAT_PCM_16;
179                          break;
180         }                
181         sf_info.format = SF_FORMAT_WAV|short_mask;
182 
183         if ((info->sf = sf_open (info->path, SFM_WRITE, &sf_info)) == NULL) {
184                 char errstr[256];
185                 sf_error_str (0, errstr, sizeof (errstr) - 1);
186                 fprintf (stderr, "cannot open sndfile \"%s\" for output (%s)\n", info->path, errstr);
187                 jack_client_close (info->client);
188                 exit (1);
189         }
190 
191         info->duration *= sf_info.samplerate;
192         info->can_capture = 0;
193 
194         pthread_create (&info->thread_id, NULL, disk_thread, info);
195 }
196 
197 void
198 run_disk_thread (thread_info_t *info)
199 {
200         info->can_capture = 1;
201         pthread_join (info->thread_id, NULL);
202         sf_close (info->sf);
203         if (overruns > 0) {
204                 fprintf (stderr,
205                          "jackrec failed with %ld overruns.\n", overruns);
206                 fprintf (stderr, " try a bigger buffer than -B %"
207                          PRIu32 ".\n", info->rb_size);
208                 info->status = EPIPE;
209         }
210         if (info->status) {
211                 unlink (info->path);
212         }
213 }
214 
215 void
216 setup_ports (int sources, char *source_names[], thread_info_t *info)
217 {
218         unsigned int i;
219         size_t in_size;
220 
221         /* Allocate data structures that depend on the number of ports. */
222         nports = sources;
223         ports = (jack_port_t **) malloc (sizeof (jack_port_t *) * nports);
224         in_size =  nports * sizeof (jack_default_audio_sample_t *);
225         in = (jack_default_audio_sample_t **) malloc (in_size);
226         rb = jack_ringbuffer_create (nports * sample_size * info->rb_size);
227 
228         /* When JACK is running realtime, jack_activate() will have
229          * called mlockall() to lock our pages into memory.  But, we
230          * still need to touch any newly allocated pages before
231          * process() starts using them.  Otherwise, a page fault could
232          * create a delay that would force JACK to shut us down. */
233         memset(in, 0, in_size);
234         memset(rb->buf, 0, rb->size);
235 
236         for (i = 0; i < nports; i++) {
237                 char name[64];
238 
239                 sprintf (name, "input%d", i+1);
240 
241                 if ((ports[i] = jack_port_register (info->client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0)) == 0) {
242                         fprintf (stderr, "cannot register input port \"%s\"!\n", name);
243                         jack_client_close (info->client);
244                         exit (1);
245                 }
246         }
247 
248         for (i = 0; i < nports; i++) {
249                 if (jack_connect (info->client, source_names[i], jack_port_name (ports[i]))) {
250                         fprintf (stderr, "cannot connect input port %s to %s\n", jack_port_name (ports[i]), source_names[i]);
251                         jack_client_close (info->client);
252                         exit (1);
253                 } 
254         }
255 
256         info->can_process = 1;          /* process() can start, now */
257 }
258 
259 int
260 main (int argc, char *argv[])
261 
262 {
263         jack_client_t *client;
264         thread_info_t thread_info;
265         int c;
266         int longopt_index = 0;
267         extern int optind, opterr;
268         int show_usage = 0;
269         char *optstring = "d:f:b:B:h";
270         struct option long_options[] = {
271                 { "help", 0, 0, 'h' },
272                 { "duration", 1, 0, 'd' },
273                 { "file", 1, 0, 'f' },
274                 { "bitdepth", 1, 0, 'b' },
275                 { "bufsize", 1, 0, 'B' },
276                 { 0, 0, 0, 0 }
277         };
278 
279         memset (&thread_info, 0, sizeof (thread_info));
280         thread_info.rb_size = DEFAULT_RB_SIZE;
281         opterr = 0;
282 
283         while ((c = getopt_long (argc, argv, optstring, long_options, &longopt_index)) != -1) {
284                 switch (c) {
285                 case 1:
286                         /* getopt signals end of '-' options */
287                         break;
288 
289                 case 'h':
290                         show_usage++;
291                         break;
292                 case 'd':
293                         thread_info.duration = atoi (optarg);
294                         break;
295                 case 'f':
296                         thread_info.path = optarg;
297                         break;
298                 case 'b':
299                         thread_info.bitdepth = atoi (optarg);
300                         break;
301                 case 'B':
302                         thread_info.rb_size = atoi (optarg);
303                         break;
304                 default:
305                         fprintf (stderr, "error\n");
306                         show_usage++;
307                         break;
308                 }
309         }
310 
311         if (show_usage || thread_info.path == NULL || optind == argc) {
312                 fprintf (stderr, "usage: jackrec -f filename [ -d second ] [ -b bitdepth ] [ -B bufsize ] port1 [ port2 ... ]\n");
313                 exit (1);
314         }
315 
316         if ((client = jack_client_new ("jackrec")) == 0) {
317                 fprintf (stderr, "jack server not running?\n");
318                 exit (1);
319         }
320 
321         thread_info.client = client;
322         thread_info.channels = argc - optind;
323         thread_info.can_process = 0;
324 
325         setup_disk_thread (&thread_info);
326 
327         jack_set_process_callback (client, process, &thread_info);
328         jack_on_shutdown (client, jack_shutdown, &thread_info);
329 
330         if (jack_activate (client)) {
331                 fprintf (stderr, "cannot activate client");
332         }
333 
334         setup_ports (argc - optind, &argv[optind], &thread_info);
335 
336         run_disk_thread (&thread_info);
337 
338         jack_client_close (client);
339 
340         jack_ringbuffer_free (rb);
341 
342         exit (0);
343 }
344 