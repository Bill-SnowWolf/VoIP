#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <sys/stat.h>
#include <arpa/inet.h>

#include <errno.h>
#include <math.h>
#include <jack/jack.h>


#define PORT 8888
#define PORT_IN 8889

typedef jack_default_audio_sample_t sample_t;

const double PI = 3.14;

jack_client_t *client;
jack_port_t *output_port;
jack_port_t *input_port;
jack_nframes_t wave_length;
sample_t *wave;
long offset = 0;
long head = 0;
long tail = 0;
jack_nframes_t wave_max_length = 10240;

// Output Server Socket
int sockfd;
struct sockaddr_in serv_addr;
int addr_len = sizeof(serv_addr);

// Input Server Socket
int sockfd_in;
struct sockaddr_in serv_addr_in;
int addr_len_in = sizeof(serv_addr_in);

void process_audio (jack_nframes_t nframes)  {
    jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);
    sample_t *input_buffer = (sample_t *)malloc(sizeof(sample_t) * nframes);
    memcpy(input_buffer, in, nframes * sizeof(sample_t));

    // Send Data: UDP
    int data_left = nframes;

    while (data_left > 0) {
        int data_size;
        if (data_left >= 512) {
            data_size = 512;
        } else {
            data_size = data_left;
        }

        int n = sendto(sockfd, input_buffer + (nframes - data_left), data_size * sizeof(sample_t), 0,
                       (struct sockaddr *)&serv_addr,
                       addr_len);
        if (n<0) {
            printf("Die\n");
            exit(1);
        }
        data_left -= data_size;
    }

    free(input_buffer);

    /*
     * Play Back
     */
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

    jack_client_t *client;

    /* Initial Jack setup, get sample rate */
    if ((client = jack_client_open("client", JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }

    jack_set_process_callback (client, process, 0);

    // Socket
    int n;
    struct hostent *server;

    if (argc < 2) {
        server = gethostbyname("127.0.0.1");
    } else {
        server = gethostbyname(argv[1]);
    }

    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    /*
     * Create client socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        fprintf(stderr, "ERROR opening socket");
        exit(1);
    }
    
    /*
     * Create server address for output socket
     */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(PORT);

    /*
     * Create Input Socket
     */
    sockfd_in = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd_in < 0) {
        fprintf(stderr, "ERROR opening socket");
        exit(1);
    }
    
    /*
     * Create server address
     */

    bzero((char *) &serv_addr_in, sizeof(serv_addr_in));
    serv_addr_in.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr_in.sin_addr.s_addr,
         server->h_length);
    serv_addr_in.sin_port = htons(PORT_IN);

    printf("==============================\n");
    printf("Tells Server that I'm Ready...\n");
    printf("==============================\n");

    n = sendto(sockfd_in, "Start!", strlen("Start!"), 0,
                       (struct sockaddr *)&serv_addr_in,
                       addr_len_in);

    /*
     * Start communication
     */

    if (jack_activate (client)) {
        fprintf (stderr, "cannot activate client");
        return 1;
    }

    const char **ports;
    
    /* create two ports: 1 input & 1 output*/
    input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    output_port = jack_port_register (client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

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

    // Receiving voice
    sample_t * buffer = (sample_t *)malloc(512 * sizeof(sample_t));
    jack_nframes_t wave_size = 0;
    

    printf("Start communication...\n");


    head = 0;
    wave = (sample_t *)malloc(wave_max_length * sizeof(sample_t));

    while (1) {    
        bzero(buffer, 512 * sizeof(sample_t));
        int n;
        
        if ((n = recvfrom(sockfd_in, buffer, 512 * sizeof(sample_t), 0, 
                                 (struct sockaddr *)&serv_addr_in, (socklen_t *)&addr_len_in)) < 0) {
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
    }

    close(sockfd);
    jack_client_close (client);
    printf("Client socket has stopped.\n");   
    return 0;
}