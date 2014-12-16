#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <jack/jack.h>
// #include <jack/transport.h>
#include <string.h>

typedef jack_default_audio_sample_t sample_t;

const double PI = 3.14;

jack_client_t *client;
jack_port_t *output_port;
unsigned long sr;
int freq = 880;
int bpm;
jack_nframes_t tone_length, wave_length;
sample_t *wave;
long offset = 0;

void process_audio (jack_nframes_t nframes)  {
    sample_t *buffer = (sample_t *) jack_port_get_buffer (output_port, nframes);
    jack_nframes_t frames_left = nframes;

    while (wave_length - offset < frames_left) {
        memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * (wave_length - offset));
        frames_left -= wave_length - offset;
        offset = 0;
    }
    if (frames_left > 0) {
        memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * frames_left);
        offset += frames_left;
    }
}

int process (jack_nframes_t nframes, void *arg) {
    process_audio (nframes);
    return 0;
}

int sample_rate_change () {
    printf("Sample rate has changed! Exiting...\n");
    exit(-1);
}

sample_t * generate_wave(jack_nframes_t wave_length, 
                         jack_nframes_t tone_length,
                         int attack_length,
                         double max_amp,
                         int decay_length,
                         sample_t scale) {

    sample_t * wave;
    double * amp;
    /* Build the wave table */
    wave = (sample_t *) malloc (wave_length * sizeof(sample_t));
    amp = (double *) malloc (tone_length * sizeof(double));

    int i;
    for (i = 0; i < attack_length; i++) {
        amp[i] = max_amp * i / ((double) attack_length);
    }
    for (i = attack_length; i < (int)tone_length - decay_length; i++) {
        amp[i] = max_amp;
    }
    for (i = (int)tone_length - decay_length; i < (int)tone_length; i++) {
        amp[i] = - max_amp * (i - (double) tone_length) / ((double) decay_length);
    }
    for (i = 0; i < (int)tone_length; i++) {
        wave[i] = amp[i] * sin (scale * i);
    }
    for (i = tone_length; i < (int)wave_length; i++) {
        wave[i] = 0;
    }

    for (i = 0; i<100;i++) {
        printf("%f\n", wave[i]);
    }
    return wave;

}

int main (int argc, char *argv[]) {
    sample_t scale;
    int i, attack_length, decay_length;
    // double *amp;
    double max_amp = 0.5;
    int attack_percent = 1, decay_percent = 10, dur_arg = 100;
    char *client_name = 0;
    char *bpm_string = "120_bpm";

    bpm = 120;


    /* Initial Jack setup, get sample rate */
    if (!client_name) {
        client_name = (char *) malloc (9 * sizeof (char));
        strcpy (client_name, "metro");
    }
    
    if ((client = jack_client_open(client_name, JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }
    jack_set_process_callback (client, process, 0);
    output_port = jack_port_register (client, bpm_string, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    sr = jack_get_sample_rate (client);

    /* setup wave table parameters */
    wave_length = 60 * sr / bpm;
    tone_length = sr * dur_arg / 1000;
    attack_length = tone_length * attack_percent / 100;
    decay_length = tone_length * decay_percent / 100;
    scale = 2 * PI * freq / sr;

    if (tone_length >= wave_length) {
        fprintf (stderr, "invalid duration (tone length = %" PRIu32
            ", wave length = %" PRIu32 "\n", tone_length,
            wave_length);
        return -1;
    }
    if (attack_length + decay_length > (int)tone_length) {
        fprintf (stderr, "invalid attack/decay\n");
        return -1;
    }

    wave = generate_wave(wave_length, tone_length, attack_length, max_amp, decay_length, scale);
    
    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        return 1;
    }

    const char **ports;
    
    if ((ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
      fprintf(stderr, "Cannot find any physical playback ports\n");
      exit(1);
    }

    if (jack_connect (client, jack_port_name(output_port), ports[0])) {
        fprintf (stderr, "cannot connect output ports\n");
    }
    printf("%s\n", ports[0]);
    free (ports);

    while (1) {
        sleep(1);
    };
}