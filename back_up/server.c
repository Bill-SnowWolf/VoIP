#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <dirent.h>

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


// Get list of files of current directory
// Assume results are less than 255 bytes
int ls(char files[255]) {
    DIR *d;
    struct dirent *dir;
    d = opendir(".");
    char ls_result[255];
    bzero(ls_result, sizeof(ls_result));

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            strcat(ls_result, dir->d_name);
            strcat(ls_result, " ");
        }

        closedir(d);
        strcpy(files, ls_result);
        return 0;
    } else {
        return -1;
    }
}


int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr;

    /*
     * Create Server Socket
     */
    int sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "ERROR opening socket");
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));

    /*
     * Bind Socket to a port
     */
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)  {
        fprintf(stderr, "ERROR on binding");
    }

    /*
     * Listen for client socket
     */
    listen(sockfd, 5);

    printf("Server Socket Started...\n");

    int client_sockfd;
    struct sockaddr_in client_addr;
    socklen_t clilen;
    char buffer[256];

    while (1) {
        printf("Waiting for new socket\n");
        clilen = sizeof(client_addr);    
        if ((client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &clilen)) < 0) {
            fprintf(stderr, "accept() failed\n");
        }

        printf("New Socket Connection Accepted...\n");

        // Start communicating
        while (1) {
            bzero(buffer, 256);
            if (read(client_sockfd, buffer, 255) < 0) {
                fprintf(stderr, "ERROR reading from socket");
                break;
            }
            printf("\n====================\n");
            printf("Request Received: %s\n", buffer);            

            // Check commands
            if (strcmp(buffer, "ls\n") == 0) {  // ls command
                char files[255];
                if (ls(files) == 0) {
                    write(client_sockfd, files, sizeof(files));
                } else {
                    write(client_sockfd, "-1", sizeof("-1"));
                }
            } else if (strcmp(buffer, "exit\n") == 0) {
                break;
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
                    perror("Invalid requests.\n");
                    break;
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

                    // Send file size in first buffer
                    bzero(buffer, 256);
                    sprintf(buffer, "%d", fsize(arg));
                    write(client_sockfd, buffer, sizeof(buffer));

                    /* Open the file that we wish to transfer */
                    FILE *fp = fopen(arg,"ra");
                    if(fp==NULL) {
                        printf("File opern error");
                        return 1;   
                    }   

                    /* Read data from file and send it */
                    while(1) {
                        /* First read file in chunks of 256 bytes */

                        bzero(buffer, 256);

                        int nread = fread(buffer, sizeof(char), sizeof(buffer), fp);

                        /* If read was success, send data. */
                        if(nread > 0) {
                            write(client_sockfd, buffer, nread);
                        }

                        if (feof(fp)) {
                            printf("End of file\n");
                            break;
                        }

                        if (ferror(fp)) {
                            printf("Error reading\n");
                            break;
                        }
                    }

                    fclose(fp);
                } else if (strcmp(command, "put") == 0) {
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

                    bzero(buffer, 256);
                    int fileSize = 0;
                    // Get File size first
                    if (read(client_sockfd, buffer, sizeof(buffer)) > 0) {
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

                    while((bytesReceived = read(client_sockfd, buffer, sizeof(buffer))) > 0) {
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
                } else {
                    printf("Invalid request.\n");
                }

            }

        }
        close(client_sockfd);
        printf("Client socket closed\n");
    }
    
    close(sockfd);
    return 0; 
}