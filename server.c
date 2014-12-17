#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dirent.h>
#include <math.h>

#include <jack/jack.h>

#define PORT 8888

typedef jack_default_audio_sample_t sample_t;

jack_client_t *client;
jack_port_t *output_port;;
jack_nframes_t wave_length;
jack_nframes_t wave_max_length = 10240;
sample_t *wave;
long head = 0;
long tail = 0;
long offset = 0;

const double PI = 3.14;

struct sockaddr_in serv_addr;
struct sockaddr_in client_addr;
int addr_len = sizeof(client_addr);

int sockfd;


void process_audio (jack_nframes_t nframes)  {
    // printf("%d\n", nframes);
    // if (wave_length > 0)
    sample_t *buffer = (sample_t *) jack_port_get_buffer (output_port, nframes);
    bzero(buffer, sizeof(sample_t) * nframes);
    // if (head < wave_length) {
    jack_nframes_t frames_left = nframes;

    while (frames_left > 0) {
        if (tail > head && tail - head > frames_left) {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (frames_left));
            head += frames_left;
            frames_left = 0;
        } else if (tail < head) {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (wave_max_length - head));
            // head = wave_length;
            frames_left -= (wave_max_length - head);
            head = 0;
        } else {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (tail - head));
            // head = wave_length;
            frames_left -= (tail - head);
            head = tail;
            break;
        }
    }


    /* 
     * Simulate send back 
     */
    int n = sendto(sockfd, "hello", strlen("hello"), 0,
                       (struct sockaddr *)&client_addr,
                       addr_len);
    
        // while (wave_length - offset < frames_left) {
        //     memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * (wave_length - offset));
        //     frames_left -= wave_length - offset;
        //     offset = 0;
        // }
        // if (frames_left > 0) {
        //     memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * frames_left);
        //     offset += frames_left;
        // }
    // } else {
        // bzero(buffer, sizeof(sample_t) * nframes);
    // }
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

    return wave;

}


int main(int argc, char *argv[]) {

    // Play Sound    
    if ((client = jack_client_open("server", JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }
    jack_set_process_callback (client, process, 0);
    output_port = jack_port_register (client, "120_bpm", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    // /*
    //  * Generate sample data
    //  */

    // unsigned long sr;
    // int freq = 880;
    // int bpm = 120;
    // jack_nframes_t tone_length;
    // // sample_t *wave;

    // sample_t scale;
    // int i, attack_length, decay_length;
    // double max_amp = 0.5;
    // int attack_percent = 1, decay_percent = 10, dur_arg = 100;
    // char *bpm_string = "120_bpm";
    
    // sr = jack_get_sample_rate (client);

    // // jack_client_close (client);

    // /* setup wave table parameters */
    // wave_length = 60 * sr / bpm;
    // tone_length = sr * dur_arg / 1000;
    // attack_length = tone_length * attack_percent / 100;
    // decay_length = tone_length * decay_percent / 100;
    // scale = 2 * PI * freq / sr;

    // if (tone_length >= wave_length) {
    //     fprintf (stderr, "invalid duration (tone length = %" PRIu32
    //         ", wave length = %" PRIu32 "\n", tone_length,
    //         wave_length);
    //     return -1;
    // }
    // if (attack_length + decay_length > (int)tone_length) {
    //     fprintf (stderr, "invalid attack/decay\n");
    //     return -1;
    // }

    // sample_t * wave1 = generate_wave(wave_length, tone_length, attack_length, max_amp, decay_length, scale);




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





    /*
     * Create Server Socket
     */

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        fprintf(stderr, "ERROR opening socket");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));

    /*
     * Bind Socket to a port
     */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)  {
        fprintf(stderr, "ERROR on binding");
    }

    /*
     * Listen for client socket: TCP/IP
     */
    // listen(sockfd, 5);

    // printf("Server Socket Started...\n");

    // int client_sockfd;
    // struct sockaddr_in client_addr;
    // socklen_t clilen;

    // char buffer[256];

    // while (1) {

        /*
         * TCP/IP
         */
        // clilen = sizeof(client_addr);    
        // if ((client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &clilen)) < 0) {
        //     fprintf(stderr, "accept() failed\n");
        // }

        // printf("New Socket Connection Accepted...\n");

        printf("Waiting for data\n");
        fflush(stdout);

        sample_t * buffer = (sample_t *)malloc(512 * sizeof(sample_t));
        jack_nframes_t wave_size = 0;
        // Start communicating

        head = 0;
        wave = (sample_t *)malloc(wave_max_length * sizeof(sample_t));

        while (1) {

            // bzero(buffer, 256);
            
            bzero(buffer, 512 * sizeof(sample_t));
            int n;
            // if ((n=read(client_sockfd, buffer, 256 * sizeof(sample_t))) < 0) {
            //     fprintf(stderr, "ERROR reading from socket");
            //     break;
            // } else if (n == 0) {
            //     break;
            // }
            
            if ((n = recvfrom(sockfd, buffer, 512 * sizeof(sample_t), 0, 
                                     (struct sockaddr *)&client_addr, (socklen_t *)&addr_len)) < 0) {
                fprintf(stderr, "ERROR reading from socket");
            }

            // printf("Received: %d\n", n);

            int buffer_length = n / sizeof(sample_t);

            if (tail + buffer_length <= wave_max_length) {
                memcpy(wave + tail, buffer, n);
                tail += buffer_length;
            } else {
                memcpy(wave + tail, buffer, (wave_max_length - tail) * sizeof(sample_t));                
                memcpy(wave, buffer + (wave_max_length - tail), sizeof(sample_t) * (tail + buffer_length - wave_max_length));
                tail = tail + buffer_length - wave_max_length;
            }

            // if (tail > 10000) {
                // break;
            // }

            // printf("%ld, %ld, %d\n", head, tail, n);

            // wave_size += n;
            // wave_length = wave_size / sizeof(sample_t);
            // free(tmp);

            // printf("%d\n", wave_length);
            // if (wave_size > 88200) {
                // break;
            // }
            // for (int i=0;i<50;i++) {
              // printf("%f\n", buffer[i]);
            // }

            // printf("\n====================\n");
            // printf("Request Received: %s\n", buffer);            
        }
        // wave_length = wave_max_length;
        free(buffer);

        // close(client_sockfd);
        printf("Client socket closed\n");
    // }
    // wave_length = wave_size / sizeof(sample_t);
    close(sockfd);


    // for (int i=0;i<100;i++) {
        // printf("%.10f, %.10f, %d\n", wave[i], wave1[i], i);
    // }
    // wave = wave1;

 

    while (1) {
        sleep(1);
    };

    jack_client_close (client);
    free(wave);
    return 0; 
}