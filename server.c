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
#define PORT_OUT 8889

typedef jack_default_audio_sample_t sample_t;

jack_client_t *client;
jack_port_t *output_port;
jack_port_t *input_port;
jack_nframes_t wave_length;
jack_nframes_t wave_max_length = 10240;
sample_t *wave;
long head = 0;
long tail = 0;
long offset = 0;

const double PI = 3.14;

int started = 0;

// Input Server Socket
struct sockaddr_in serv_addr;
struct sockaddr_in client_addr;
int addr_len = sizeof(client_addr);

int sockfd;

// Output Server Socket
struct sockaddr_in serv_addr_out;
struct sockaddr_in client_addr_out;
int addr_len_out = sizeof(client_addr_out);

int sockfd_out;

int n;


void process_audio (jack_nframes_t nframes)  {
    sample_t *buffer = (sample_t *) jack_port_get_buffer (output_port, nframes);
    bzero(buffer, sizeof(sample_t) * nframes);
    jack_nframes_t frames_left = nframes;

    while (frames_left > 0) {
        if (tail > head && tail - head > frames_left) {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (frames_left));
            head += frames_left;
            frames_left = 0;
        } else if (tail < head) {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (wave_max_length - head));
            frames_left -= (wave_max_length - head);
            head = 0;
        } else {
            memcpy(buffer + (nframes - frames_left), wave + head, sizeof(sample_t) * (tail - head));
            frames_left -= (tail - head);
            head = tail;
            break;
        }
    }


    /* 
     * Send Back
     */

    jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

    sample_t *input_buffer = (sample_t *)malloc(sizeof(sample_t) * nframes);

    memcpy(input_buffer, in, nframes * sizeof(sample_t));
    // Send Back Sound
    if (started == 1) {
        int data_left = nframes;

        while (data_left > 0) {
            int data_size;
            if (data_left >= 512) {
                data_size = 512;
            } else {
                data_size = data_left;
            }

            int n = sendto(sockfd_out, input_buffer + (nframes - data_left), data_size * sizeof(sample_t), 0,
                           (struct sockaddr *)&client_addr_out,
                           addr_len_out);
            if (n<0) {
                printf("Die\n");
                exit(1);
            }
            data_left -= data_size;
        }

        free(input_buffer);
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

int main(int argc, char *argv[]) {

    /*
     * Set up Jack Server before connecting sockets
     */ 
    if ((client = jack_client_open("server", JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }


    jack_set_process_callback (client, process, 0);

    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        return 1;
    }

    const char **ports;
    
    /* create two ports: 1 input & 1 output*/
    input_port = jack_port_register (client, "server_input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    output_port = jack_port_register (client, "server_output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    /* tell the JACK server that we are ready to roll */
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

    /*
     * Create Input Server Socket
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
     * Create Output Server Socket
     */

    sockfd_out = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd_out < 0) {
        fprintf(stderr, "ERROR opening socket");
    }

    bzero((char *) &serv_addr_out, sizeof(serv_addr_out));

    /*
     * Bind Socket to a port
     */
    serv_addr_out.sin_family = AF_INET;
    serv_addr_out.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr_out.sin_port = htons(PORT_OUT);
    if (bind(sockfd_out, (struct sockaddr *) &serv_addr_out, sizeof(serv_addr_out)) < 0)  {
        fprintf(stderr, "ERROR on binding");
    }

    printf("Waiting for client\n");
    fflush(stdout);
    char command_buff[256];
    while(started == 0) {
        bzero(command_buff, 256);
        if ((n = recvfrom(sockfd_out, command_buff, 256 * sizeof(sample_t), 0, 
                         (struct sockaddr *)&client_addr_out, (socklen_t *)&addr_len_out)) < 0) {
            fprintf(stderr, "ERROR reading from socket");
        }
        if (strcmp(command_buff, "Start!") == 0) {
            started = 1;
            break;
        }    
    }

    printf("Client connected, start communicating...\n");
    fflush(stdout);

    sample_t * buffer = (sample_t *)malloc(512 * sizeof(sample_t));
    jack_nframes_t wave_size = 0;
    // Start communicating

    head = 0;
    wave = (sample_t *)malloc(wave_max_length * sizeof(sample_t));

    while (1) {            
        bzero(buffer, 512 * sizeof(sample_t));
        
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
    
    }
    free(buffer);

    close(sockfd);

    jack_client_close (client);
    free(wave);
    return 0; 
}