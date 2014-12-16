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

#define PORT 8888

uint32_t fsize(const char *filename) {
    struct stat st; 

    if (stat(filename, &st) == 0) {
        return (uint32_t)st.st_size;
    }

    return -1; 
}

int stringToInt(char a[]) {
  int c, sign, offset, n;
 
  if (a[0] == '-') {  // Handle negative integers
    sign = -1;
  }
 
  if (sign == -1) {  // Set starting position to convert
    offset = 1;
  }
  else {
    offset = 0;
  }
 
  n = 0;
 
  for (c = offset; a[c] != '\0'; c++) {
    n = n * 10 + a[c] - '0';
  }
 
  if (sign == -1) {
    n = -n;
  }
 
  return n;
}

int main(int argc, char *argv[]) {
    int sockfd, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    if (argc < 2) {
         fprintf(stderr,"ERROR, no host provided\n");
         exit(1);
     }

    /*
     * Create client socket
     */
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "ERROR opening socket");
        exit(1);
    }
    
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    printf("%s\n", (server->h_addr));


    /*
     * Create server address
     */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    // serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");    
    // printf("%d\n",inet_addr("127.0.0.1"));
    // printf("%d\n", serv_addr.sin_addr.s_addr);
    serv_addr.sin_port = htons(PORT);

    /*
     * Establish connection
     */

    if (connect(sockfd,(struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "ERROR connecting");
        exit(1);
    }

    FILE *fp;
    fp = fopen("sample_file.txt", "ab"); 
    if(NULL == fp)
    {
        printf("Error opening file");
        return 1;
    }

    /*
     * Start communication
     */

    char buffer[256];
    while (1) {
        printf("client> ");
        bzero(buffer, 256);
        fgets(buffer, 255, stdin);

        // Stop client socket
        if (strcmp(buffer, "exit\n") == 0) {    
            break;
        }

        if (strcmp(buffer, "ls\n") == 0) {
            n = write(sockfd, buffer, strlen(buffer));
            if (n < 0) {
                fprintf(stderr, "ERROR writing to socket");
                break;
            }
            bzero(buffer, 256);
            if ((read(sockfd, buffer, sizeof(buffer)) > 0) && (strcmp(buffer, "-1")!=0)) {
                printf("%s\n", buffer);
            } else {
                fprintf(stderr, "ls command error.\n");
            }

        } else {
            // Check if this is get command
            // Assume there is no extra space at the beginning
            char* pch;
            pch = strchr(buffer,' ');

            int commandLength;
            if (pch!=NULL) {
                commandLength = pch - buffer;
            }
            else {
                // There should be at least one arguments for get
                perror("Missing arguments. (expected 1)\n");
                continue;
            }

            char* command = malloc((commandLength + 1) * sizeof(char));
            memcpy(command, &buffer[0], commandLength);
            command[commandLength] = '\0';

            if (strcmp(command, "get") == 0) {
                // This is get command
                // Get file name 
                char* argPtr = strchr(pch + 1,' ');

                int argLength;
                if (argPtr!=NULL) {
                    argLength = argPtr - pch - 1;
                } else {
                    argLength = strlen(buffer) - commandLength - 1;
                }

                char* arg = malloc(argLength * sizeof(char));
                memcpy(arg, &buffer[commandLength + 1], argLength - 1);
                arg[argLength] = '\0';

                // Send Request
                n = write(sockfd, buffer, strlen(buffer));
                if (n < 0) {
                    fprintf(stderr, "ERROR writing to socket");
                    break;
                }
                
                bzero(buffer, 256);
                int fileSize = 0;
                // Get File size first
                if (read(sockfd, buffer, sizeof(buffer)) > 0) {
                    fileSize = stringToInt(buffer);
                } else {
                    perror("[ERROR] fail to read file size from server.\nFile transfer has stopped.\n");
                    break;
                }


                FILE *fp = fopen(arg,"wa");
                if(fp==NULL) {
                    printf("File opern error");
                    return 1;   
                }   

                int bytesReceived;
                int totalBytesReceived = 0;
                bzero(buffer, 256);

                printf("Start downloading file...\n");

                while((bytesReceived = read(sockfd, buffer, sizeof(buffer))) > 0) {
                    fwrite(buffer, sizeof(char), bytesReceived, fp);
                    totalBytesReceived = totalBytesReceived + bytesReceived;

                    if (totalBytesReceived >= fileSize) {
                        printf("Downloading file finished.\n");
                        break;
                    }
                    bzero(buffer, 256);
                }
                
                fclose(fp);

                if (bytesReceived < 0) {
                    fprintf(stderr, "ERROR reading from socket");
                    break;
                }
            } else if (strcmp(command, "put") == 0) {
                // This is put command
                // Get file name 
                char* argPtr = strchr(pch + 1,' ');

                int argLength;
                if (argPtr!=NULL) {
                    argLength = argPtr - pch - 1;
                } else {
                    argLength = strlen(buffer) - commandLength - 1;
                }

                char* arg = malloc(argLength * sizeof(char));
                memcpy(arg, &buffer[commandLength + 1], argLength - 1);
                arg[argLength] = '\0';

                // Send Command
                n = write(sockfd, buffer, strlen(buffer));
                if (n < 0) {
                    fprintf(stderr, "ERROR writing to socket");
                    break;
                }

                // Send file size in first buffer
                bzero(buffer, 256);
                sprintf(buffer, "%d", fsize(arg));
                write(sockfd, buffer, sizeof(buffer));

                /* Open the file that we wish to transfer */
                FILE *fp = fopen(arg,"ra");
                if(fp==NULL) {
                    perror("File opern error\n");
                    continue;   
                }   

                /* Read data from file and send it */
                while(1) {
                    /* First read file in chunks of 256 bytes */

                    bzero(buffer, 256);

                    int nread = fread(buffer, sizeof(char), sizeof(buffer), fp);

                    /* If read was success, send data. */
                    if(nread > 0) {
                        write(sockfd, buffer, nread);
                    }

                    if (feof(fp)) {
                        break;
                    }

                    if (ferror(fp)) {
                        printf("Error reading\n");
                        break;
                    }
                }
                fclose(fp);

            } else {
                fprintf(stderr, "Invalid Command.\n");
            }
            
        }        
    }
    close(sockfd);
    printf("Client socket has stopped.\n");   
    return 0;
}