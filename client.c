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
    // printf("%d\n", nframes);
    // sample_t *buffer = (sample_t *) jack_port_get_buffer (output_port, nframes);

    jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);

    sample_t *input_buffer = (sample_t *)malloc(sizeof(sample_t) * nframes);
    // jack_nframes_t frames_left = nframes;

    // while (wave_length - offset < frames_left) {
    //     memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * (wave_length - offset));
    //     frames_left -= wave_length - offset;
    //     offset = 0;
    // }
    // if (frames_left > 0) {
    //     memcpy (buffer + (nframes - frames_left), wave + offset, sizeof (sample_t) * frames_left);
    //     offset += frames_left;
    // }
    memcpy(input_buffer, in, nframes * sizeof(sample_t));

    // // Send buffer through socket: TCP
    // int n = write(sockfd, buffer, nframes * sizeof(sample_t));
    // if (n < 0) {
    //     printf("Socket closed \n");
    //     close(sockfd);
    // }

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

    // jack_default_audio_sample_t *in = (jack_default_audio_sample_t *) jack_port_get_buffer (input_port, nframes);
    // Send buffer through socket
    
    // if (n < 0) {
        // printf("Socket closed \n");
        // close(sockfd);
    // }

// jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);
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

    jack_client_t *client;

    /*
     * Generate sample data
     */

    // unsigned long sr;
    // int freq = 880;
    // int bpm = 120;
    // jack_nframes_t tone_length;
    // // sample_t *wave;

    // sample_t scale;
    // int i, attack_length, decay_length;
    // double max_amp = 0.5;
    // int attack_percent = 1, decay_percent = 10, dur_arg = 100;
    // char *client_name = "metro";
    // char *bpm_string = "120_bpm";



    /* Initial Jack setup, get sample rate */
    if ((client = jack_client_open("client", JackNullOption, NULL)) == 0) {
        fprintf (stderr, "jack server not running?\n");
        return 1;
    }
    
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

    // wave = generate_wave(wave_length, tone_length, attack_length, max_amp, decay_length, scale);    

    jack_set_process_callback (client, process, 0);
    // output_port = jack_port_register (client, "120_bpm", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    // input_port = jack_port_register (client, "input", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

    // Socket
    int n;
    
    struct hostent *server;

    // if (argc < 2) {
         // fprintf(stderr,"ERROR, no host provided\n");
         // exit(1);
     // }

    if (argc < 2) {
        server = gethostbyname("127.0.0.1");
        // server = "127.0.0.1"
    } else {
        server = gethostbyname(argv[1]);
        // server = argv[1];
    }
    // server = gethostbyname("127.0.0.1");
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
    
    
    printf("Try to connect to server.\n");
    /*
     * Create server address
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

    printf("Try to connect to server.\n");
    
    /*
     * Create server address
     */

    bzero((char *) &serv_addr_in, sizeof(serv_addr_in));
    serv_addr_in.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr_in.sin_addr.s_addr,
         server->h_length);
    serv_addr_in.sin_port = htons(PORT_IN);


    n = sendto(sockfd_in, "Start!", strlen("Start!"), 0,
                       (struct sockaddr *)&serv_addr_in,
                       addr_len_in);

    /*
     * Establish connection: TCP/IP
     */

    // if (connect(sockfd,(struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        // fprintf(stderr, "ERROR connecting");
        // exit(1);
    // }

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

    /* tell the JACK server that we are ready to roll */
    // if (jack_activate (client)) {
    //     fprintf (stderr, "cannot activate client");
    //     return 1;
    // }

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
    // Start communicating

    head = 0;
    wave = (sample_t *)malloc(wave_max_length * sizeof(sample_t));

    while (1) {    
        // sleep(1);        
        bzero(buffer, 512 * sizeof(sample_t));
        int n;
        
        if ((n = recvfrom(sockfd_in, buffer, 512 * sizeof(sample_t), 0, 
                                 (struct sockaddr *)&serv_addr_in, (socklen_t *)&addr_len_in)) < 0) {
            fprintf(stderr, "ERROR reading from socket");
        }

        printf("Received: %d\n", n);

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

    // printf("Wave Length: %d\n", wave_length);

    // int data_sent = 0;

    // while (data_sent < 10240) {
    //     n = sendto(sockfd, wave + data_sent, 256 * sizeof(sample_t), 0,
    //                    (struct sockaddr *)&serv_addr,
    //                    addr_len);
    //     if (n<0) {
    //         printf("Die\n");
    //         exit(1);
    //     }
    //     data_sent += 256;
    // }



    // sample_t * buffer = (sample_t *)malloc(256 * sizeof(sample_t));

    // int buffer_size = 256 * sizeof(sample_t);
    // // while (1) {
    //     printf("client> ");
    //     bzero(buffer, buffer_size);
    //     memcpy(buffer, wave, buffer_size);

    //     printf("2: %lu\n", wave_length * sizeof(sample_t));


    //     n = write(sockfd, wave, wave_length * sizeof(sample_t));
    //     if (n < 0) {
    //         fprintf(stderr, "ERROR writing to socket");
    //         // break;
    //     }
         
    // // }
    close(sockfd);
    printf("Client socket has stopped.\n");   
    return 0;
}