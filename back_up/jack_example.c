#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <jack/jack.h>

jack_port_t *input_port;
jack_port_t *output_port;

/* Processing thread: only transmit the data from input to output */
int process (jack_nframes_t nframes, void *arg) {
  jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);
  jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

  memcpy (out, in, sizeof (jack_default_audio_sample_t) * nframes);
  return 0;
}

void jack_shutdown (void *arg)
{
  exit(1);
}

int main ()
{
  jack_client_t *client;
  const char **ports;

  /* try to become a client of the JACK server */

  if ((client = jack_client_open("test_client", JackNullOption, NULL)) == 0) {
    fprintf (stderr, "jack server not running?\n");
    return 1;
  }

  /* tell the JACK server to call `process()' whenever there is work to be done. */
  jack_set_process_callback (client, process, 0);

  /* tell the JACK server to call `jack_shutdown()' if it ever shuts down, either entirely, or if it
     just decides to stop calling us. */
  jack_on_shutdown (client, jack_shutdown, 0);

  /* display the current sample rate. once the client is activated  */
  printf ("engine sample rate: %d\n", jack_get_sample_rate (client));

  /* create two ports: 1 input & 1 output*/
  input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  output_port = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

  /* tell the JACK server that we are ready to roll */
  if (jack_activate (client)) {
    fprintf (stderr, "cannot activate client");
    return 1;
  }

  if ((ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsOutput)) == NULL) {
    fprintf(stderr, "Cannot find any physical capture ports\n");
    exit(1);
  }

  if (jack_connect (client, ports[0], jack_port_name (input_port))) {
    fprintf (stderr, "cannot connect input ports\n");
  }

  printf("%s\n", ports[0]);
  free (ports);
   
  if ((ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
      fprintf(stderr, "Cannot find any physical playback ports\n");
      exit(1);
  }

  if (jack_connect (client, jack_port_name (output_port), ports[0])) {
      fprintf (stderr, "cannot connect output ports\n");
  }
  printf("%s\n", ports[0]);
  free (ports);

  /* Since this is just a toy, run for a few seconds, then finish */
  sleep (10);
  jack_client_close (client);
  exit (0);
}