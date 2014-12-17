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


const double PI = 3.14;


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

        // while (wave_length - head < frames_left) {
        //     memcpy (buffer + (nframes - frames_left), wave + head, sizeof (sample_t) * (wave_length - head));
        //     frames_left -= wave_length - head;
        //     head = 0;
        // }
        // if (frames_left > 0) {
        //     memcpy (buffer + (nframes - frames_left), wave + head, sizeof (sample_t) * frames_left);
        //     head += frames_left;
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



int main(int argc, char *argv[]) {

    // Play Sound    
    if ((client = jack_client_open("server", JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }
    jack_set_process_callback (client, process, 0);
    output_port = jack_port_register (client, "120_bpm", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

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


    // Socket Part
    struct sockaddr_in serv_addr;
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);

    /*
     * Create Server Socket
     */
    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
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

        sample_t * buffer = (sample_t *)malloc(256 * sizeof(sample_t));
        jack_nframes_t wave_size = 0;
        // Start communicating

        head = 0;
        wave = (sample_t *)malloc(wave_max_length * sizeof(sample_t));

        while (1) {

            // bzero(buffer, 256);
            
            bzero(buffer, 256 * sizeof(sample_t));
            int n;
            // if ((n=read(client_sockfd, buffer, 256 * sizeof(sample_t))) < 0) {
            //     fprintf(stderr, "ERROR reading from socket");
            //     break;
            // } else if (n == 0) {
            //     break;
            // }
            
            if ((n = recvfrom(sockfd, buffer, 256 * sizeof(sample_t), 0, 
                                     (struct sockaddr *)&client_addr, (socklen_t *)&addr_len)) < 0) {
                fprintf(stderr, "ERROR reading from socket");
            }

            int buffer_length = n / sizeof(sample_t);

            if (tail + buffer_length <= wave_max_length) {
                memcpy(wave + tail, buffer, n);
                tail += buffer_length;
            } else {
                memcpy(wave + tail, buffer, (wave_max_length - tail) * sizeof(sample_t));                
                memcpy(wave, buffer + (wave_max_length - tail), sizeof(sample_t) * (tail + buffer_length - wave_max_length));
                tail = tail + buffer_length - wave_max_length;
            }
            printf("%ld, %ld, %d\n", head, tail, n);

            // wave_size += n;
            // wave_length = wave_size / sizeof(sample_t);
            // free(tmp);

            // printf("%d\n", wave_length);
            // if (wave_size > 88200) {
                // break;
            // }
            // for (int i=0;i<50;i++) {
            //   printf("%f\n", wave[i]);
            // }

            // printf("\n====================\n");
            // printf("Request Received: %s\n", buffer);            
        }
        free(buffer);

        // close(client_sockfd);
        printf("Client socket closed\n");
    // }
    // wave_length = wave_size / sizeof(sample_t);
    close(sockfd);


    // while (1) {
        // sleep(1);
    // };

    free(wave);
    return 0; 
}