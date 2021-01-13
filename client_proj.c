/*
please provide IP Address and Port number
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <netdb.h>

#define MAXDATASIZE 1024

void error(const char *msg){
    perror(msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]){

    if(argc != 3){
        fprintf(stderr,"Usage: <%s> <IP Addr> <Port>\r\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd, std_in = 0;
    int maxfd, nready, i,j;
    struct sockaddr_in servAddr;
    struct hostent *he;
    unsigned char flag[7] = {0x3C,0x3C,0x2D,0x45,0x4F,0x46};
    ssize_t retval;
    char recvbuf[MAXDATASIZE];
    fd_set readfds, allset;
    FILE *fp;
    char *arg;
    unsigned char filebuf[MAXDATASIZE];
    bzero(filebuf, sizeof(filebuf));

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        error("<!> Error: socket\r\n");

    bzero(&servAddr, sizeof(struct sockaddr_in));
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(atoi(argv[2]));
    he = gethostbyname(argv[1]);
    servAddr.sin_addr = *((struct in_addr*)he->h_addr);

    int en = 1; //enable option passed to optname parameter
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &en, sizeof(int)) == -1)
        error("<!> Error: setsockopt(SO_REUSEADDR)\r\n");
    if(connect(sockfd, (struct sockaddr*)&servAddr,
               sizeof(struct sockaddr_in)) == -1)
        error("<!> Error: connect\r\n");

    printf("~> connected to %s:%d\r\n",
           inet_ntoa(servAddr.sin_addr), ntohs(servAddr.sin_port));

    FD_ZERO(&allset);
    FD_SET(std_in, &allset);
    FD_SET(sockfd, &allset);

    maxfd = (sockfd >= std_in)? sockfd: std_in;

    while(1){

        readfds = allset;
        if((nready = select(maxfd + 1, &readfds, NULL, NULL, NULL)) < 0){
            printf("<!> Error: select\r\n");
            continue;
        } else if(!nready){
            printf("<!> Error: select timeout\r\n");
            continue;
        }
        bzero(recvbuf, MAXDATASIZE);
        if(FD_ISSET(std_in, &readfds)){

            if(fgets(recvbuf, MAXDATASIZE, stdin) == NULL)
                error("<!> Error: stdin\r\n");

            //send message to the server
            recvbuf[strlen(recvbuf)-1] = '\0';
            if(send(sockfd, recvbuf, strlen(recvbuf), 0) == -1)
                error("<!> Error: sending message\r\n");
            if(recvbuf[0] == '/'){
                arg = strtok(recvbuf, " ");
                if(!strncmp(arg, "/q",2)){
                    shutdown(std_in, SHUT_RD);
                    printf("~> keyboard not supported anymore.\r\n");
                    close(std_in);
                    FD_CLR(std_in, &allset);
                    break;
                } else if(!strncmp(arg, "/file", 5)){
                    //send the file here
                    arg = strtok(NULL, " "); //IP
                    arg = strtok(NULL, " "); //port
                    arg = strtok(NULL, " "); //filename

                    printf("sending f'%s'...\r\n", arg);
                    if((fp = fopen(arg, "rb")) == NULL)
                        error("<!> Error: fopen\r\n");

                    while(!feof(fp)){
                        i = fread(filebuf,1, MAXDATASIZE, fp);
                        if(send(sockfd, filebuf, i, 0) == -1)
                            error("<!> Error: send file\r\n");
                        bzero(filebuf,sizeof(filebuf));
                    }
                    sleep(1); //delay for one second
                    send(sockfd, flag, sizeof(flag), 0);
                    //shutdown(sockfd, SHUT_WR);
                    fclose(fp);
                    printf("<*> Success: File sent.\r\n");

                }
            }


            //printf("recvbuf: %s\r\n", recvbuf);


        } else if(FD_ISSET(sockfd, &readfds)){

            if((retval = recv(sockfd, recvbuf, MAXDATASIZE,0)) < 0)
                error("<!> Error: message received\r\n");


            if(recvbuf[0] == '/'){

                arg = strtok(recvbuf, " ");
                //create a file

                if(!strncmp(arg,"/File>", 6)){
                    arg = strtok(NULL, " ");

                    if((fp = fopen(arg, "wb")) == NULL)
                        error("<!> Error: fopen\r\n");

                    printf("\n\t\t<*> Receiving f'%s' from", arg);
                    arg = strtok(NULL, "[");
                    arg = strtok(NULL, "]");
                    printf(" %s:\r\n", arg);

                    while(1){
                        if((i = recv(sockfd, filebuf, MAXDATASIZE,0)) < 0)
                            error("<!> Error: receiving file\r\n");
                        //printf("%s\r\n", filebuf);
                        for(j=0; j<6;j++){
                            if(flag[j] != filebuf[j])
                                break;
                        }

                        if(j==6) break;
                        fwrite(filebuf, 1, i, fp);
                        bzero(filebuf,sizeof(filebuf));
                    }

                    fclose(fp);
                    printf("\t\t<*> Success: File Received.\r\n");
                }
            } else{
                if(recvbuf[0] != '$' && recvbuf[0] != '@')
                    fputs("\t\t", stdout);
                puts(recvbuf);
            }
        }
    }

    close(sockfd);

    return 0;
}