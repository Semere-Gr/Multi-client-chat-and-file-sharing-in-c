/*
Server side programme
Usage: ./executable <IP address> <Port>
*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netdb.h>

#define MAXDATASIZE 1024
#define BACKLOG 5
#define MAXCLIENTS 127


//model client
typedef struct client{
    int connfd; //file descriptor of the client
    struct sockaddr_in addr;
    char *name; //name of the client
    char *data; //keep track of chat history
    int dlen; //keep track of data length
}Client;

typedef struct File_info{
    int r_fd; //receivers file descriptor
    int s_fd;
    char filename[20];
}File_info;


/*data structure for registered users*/
typedef struct {
    char s_ip[20]; //sender ip
    int s_port;  //sender port
    char *offline_msg;
    char r_ip[20]; //receiver ip
    int r_port; //receiver port
    char flag[2];
}Reg_users;

void process_cli(Client *client, char *recvbuf, int len);
void save_data(const char *recvbuf, int len, char *data, int *str_len);

void error(const char *msg){
    perror(msg);
    exit(EXIT_FAILURE);
}

void echo_message(int connfd, const char *msg){
    if(send(connfd, msg, strlen(msg),0) < 0){
        error("Error: echo_message\n");
    }
}

void strip_nr(char *buf){
    while(*buf != '\0'){
        if(*buf == '\r' || *buf == '\n')
            *buf = '\0';
        buf++;
    }
}

void send_online_clients(int connfd, Client *client){
    char temp[64];

    for(int i=0; i< MAXCLIENTS; i++){
        if(client[i].connfd != -1){
            sprintf(temp, "@ client[%s:%d]\r\n", inet_ntoa(client[i].addr.sin_addr),
                    ntohs(client[i].addr.sin_port));
            echo_message(connfd,temp);
        }
    }
}

void send_reg_clients(int connfd, Reg_users *user){
    //list all the members
    char temp[64];
    for (int i = 0; i < MAXCLIENTS; i++){
        if(user[i].r_port != -1){
            sprintf(temp, "@ client[%s:%d]\r\n", user[i].r_ip,
                    user[i].r_port);
            echo_message(connfd,temp);
        }
    }
}

void *share_file(void *arg){
    File_info fl = *(File_info*)arg;
    int i;

    unsigned char recvbuf[MAXDATASIZE];
    unsigned char flag[7] = {0x3C,0x3C,0x2D,0x45,0x4F,0x46};

    //receive the info
    bzero(recvbuf, sizeof(recvbuf));
    while((i = recv(fl.s_fd, recvbuf, MAXDATASIZE, 0)) > 0){

        if((send(fl.r_fd, recvbuf,i, 0)) < 0)
            error("<!> Error: send\r\n");
        //puts(recvbuf);
        for(i = 0; i<6; i++){
            if(recvbuf[i] != flag[i])
                break;
        }
        if(i == 6) break;
        bzero(recvbuf, MAXDATASIZE);
    }

    pthread_exit(NULL);
}
int ip_match(struct in_addr ipa, struct in_addr ipb){

    return strcmp(inet_ntoa(ipa), inet_ntoa(ipb));

}
int is_online(char *ip, char *prt, Client *cli){
    int port = atoi(prt);
    struct hostent *he;
    he = gethostbyname(ip);
    struct in_addr IP;
    IP = *((struct in_addr*)he->h_addr);
    for(int i = 0; i < MAXCLIENTS; i++){
        if(cli[i].connfd != -1){
            //check if the ip matchs
            if(!ip_match(IP, cli[i].addr.sin_addr)){
                if(port == ntohs(cli[i].addr.sin_port)){
                    return i;
                }
            }
        }
    }

    return -1;
}

int is_regs(char *ip, char *prt, Reg_users *usr){
    int port = atoi(prt);
    struct hostent *he;
    he = gethostbyname(ip);
    struct in_addr IP;
    IP = *((struct in_addr*)he->h_addr);
    for(int i=0; i < MAXCLIENTS; i++){
        if(usr[i].flag[0] == 'l'){
            if(!strcmp(inet_ntoa(IP), usr[i].r_ip)){
                if(port == ntohs(usr[i].r_port)){
                    return i;
                }
            }
        }
    }
    return -1;
}

void public_message(Client *cli, const char *buf,int except){
    //sends a message to every member except to the sender itself.
    int i;
    for(i = 0; i< MAXCLIENTS; i++){
        if(cli[i].connfd != -1){
            if(cli[i].connfd != except){
                if(send(cli[i].connfd, buf, strlen(buf), 0) == -1)
                    error("<!> Error: message not sent\r\n");
            }
        }
    }
}

int main(int argc, char *argv[]){

    if(argc != 3){
        fprintf(stderr,"Usage: <%s> <IP Address> <Port>\r\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd, connfd, recvfd;
    struct sockaddr_in cliAddr;
    int maxfd, maxi, i, nready, k;
    char recvbuf[MAXDATASIZE], sendbuf[MAXDATASIZE];
    char *cmd, *args, *temp;
    socklen_t sin_size;
    fd_set allset, readfds;
    pthread_t tid;
    ssize_t n;
    Client client[MAXCLIENTS];
    Reg_users user[MAXCLIENTS];
    File_info param;
    struct addrinfo hints;
    struct addrinfo *result, *rp;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC; //allow ipv4 and ipv6
    hints.ai_socktype = SOCK_STREAM; //TCP socket
    hints.ai_protocol = 0; //any protocol
    hints.ai_flags = AI_PASSIVE; // allow "wildcard address". e.g. INADDR_ANY
    // node argument below must be set to NULL

    if((i = getaddrinfo(NULL, argv[2], &hints, &result)) != 0){
        fprintf(stderr, "<!> getaddrinfo error: %s", gai_strerror(i));
        exit(EXIT_FAILURE);
    }
    int optval = 1; //enable option passed to optname parameter
    //loop through the possible address structure available
    for(rp = result; rp !=NULL; rp = rp->ai_next){
        if((sockfd = socket(rp->ai_family, rp->ai_socktype,
                            rp->ai_protocol)) == -1)
            continue;
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval,
                       sizeof(int)) == -1)
            error("<!> setsockopt(SO_REUSEADDR) failed.\n");
        if(bind(sockfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;
        //close the socket we failed to bind info
        if(close(sockfd) == -1)
            error("<!> Error binding to and closing the socket\n");
    }
    if(rp == NULL)
        error("<!> Error creating and binding to the socket.\n");
    freeaddrinfo(result);

    //listen on the server socket
    if(listen(sockfd, BACKLOG) == -1)
        error("<!> Error listening\n");

    printf("<<~ Server: listening...\n");

    sin_size = sizeof(struct sockaddr_in); //for accept
    for(i = 0; i < MAXCLIENTS; i++){
        client[i].connfd = -1;
        user[i].flag[0] = 'f'; // the client is offline
        user[i].r_port = -1;
    }

    FD_ZERO(&allset);
    FD_SET(sockfd, &allset);
    maxfd = sockfd;
    maxi = -1; // the index of the client connected last
    while(1){
        readfds = allset;
        bzero(sendbuf, MAXDATASIZE);
        bzero(recvbuf, MAXDATASIZE);
        if((nready = select(maxfd + 1, &readfds, NULL, NULL, NULL)) == -1)
            error("<!> Error: select\n");

        // new incomming connection available
        if(FD_ISSET(sockfd, &readfds)){

            if((connfd = accept(sockfd,(struct sockaddr*)&cliAddr,
                                &sin_size)) == -1){
                perror("<!> Error: accept\n");
                continue;
            }
            //keep track of the new client info
            for(i = 0; i < MAXCLIENTS; i++)
                if(client[i].connfd == -1){
                    client[i].connfd = connfd;
                    client[i].addr = cliAddr;
                    client[i].name = (char *)malloc(32);
                    sprintf(client[i].name , "%d", i + 1);
                    client[i].data = (char *)malloc(MAXDATASIZE);
                    client[i].data[0] = '\0';
                    client[i].dlen = 0; //no data initially
                    printf("<~> You got a connection from %s:%d\r\n",
                           inet_ntoa(client[i].addr.sin_addr),
                           ntohs(client[i].addr.sin_port));

                    //register the user
                    sprintf(user[i].r_ip , "%s", inet_ntoa(client[i].addr.sin_addr));
                    user[i].r_port = ntohs(client[i].addr.sin_port);
                    user[i].offline_msg = (char*)malloc(MAXDATASIZE);
                    user[i].offline_msg[0] = '\0';
                    user[i].flag[0] = 'l'; //the client is live
                    break;
                } /*end if(client[i].connfd== -1)*/
            if(i == MAXCLIENTS) {
                printf("<!> Max clients reached.\n");
                continue;
            }
            FD_SET(connfd, &allset);

            if (maxfd < connfd) maxfd = connfd;
            if(i > maxi) maxi = i;
            /*if only the server was in readfds*/
            if(--nready <= 0)
                continue;

        }/* end FD_ISSET(sockfd, &readfds)*/

        //check if a clients data is ready
        for (i = 0; i <= maxi; i++){

            if((connfd = client[i].connfd) < 0) continue;
            if(FD_ISSET(connfd, &readfds)){

                if((n = recv(connfd, recvbuf, MAXDATASIZE,0)) == 0){
                    close(connfd);
                    printf("-> %s on %s:%d has left.\n", client[i].name,
                           inet_ntoa(client[i].addr.sin_addr),
                           ntohs(client[i].addr.sin_port));
                    FD_CLR(connfd, &allset);
                    client[i].connfd = -1;
                    client[i].dlen = 0;

                    sprintf(sendbuf, "@|> %s on %s:%d has left.\n", client[i].name,
                            inet_ntoa(client[i].addr.sin_addr), ntohs(client[i].addr.sin_port));
                    strcat(sendbuf, "\0");
                    public_message(client, sendbuf, client[i].connfd);

                    //free up allocated memory
                    free(client[i].name);
                    free(client[i].data);
                } else {
                    recvbuf[n] = '\0';
                    //remove the carrier return and newline

                    strip_nr(recvbuf);

                    sendbuf[0] = '\0';
                    if(recvbuf[0] == '/'){
                        cmd = strtok(recvbuf, " ");
                        //help
                        if(!strcmp(cmd,"/help")){
                            bzero(sendbuf, MAXDATASIZE);
                            strcat(sendbuf,"$ /help  \tShow operations\r\n");
                            strcat(sendbuf,"$ /als   \tList registered clients\r\n");
                            strcat(sendbuf,"$ /ls    \tList online clients\r\n");
                            strcat(sendbuf,"$ /q	 \tLogout\r\n");
                            strcat(sendbuf,"$ /name  \tRename, Usage: /name <name>\r\n");
                            strcat(sendbuf,"$ /msg   \t/msg <IP> <Port> <private msg>\r\n");
                            strcat(sendbuf,"$ /file  \t/file <IP> <Port> <filename>\r\n");
                            echo_message(connfd, sendbuf);
                        } else if(!strcmp(cmd, "/als")){
                            //list the clients registered(ip+port)
                            sprintf(sendbuf, "$> registered clients\r\n");
                            echo_message(connfd, sendbuf);
                            send_reg_clients(connfd, user);

                        } else if(!strcmp(cmd, "/ls")){
                            //list active clients(ip+port)
                            sprintf(sendbuf, "$> online clients\r\n");
                            echo_message(connfd, sendbuf);
                            send_online_clients(connfd, client);

                        } else if(!strcmp(cmd, "/q")){
                            //logout
                            //notify all clients this client has left
                            close(connfd);
                            client[i].connfd = -1;
                            user[i].flag[0] = 'f'; // client went offline
                            printf("-> %s on %s:%d has left.\n", client[i].name,
                                   inet_ntoa(client[i].addr.sin_addr),
                                   ntohs(client[i].addr.sin_port));

                            sprintf(sendbuf, "@|> %s on %s:%d has left.\n", client[i].name,
                                    inet_ntoa(client[i].addr.sin_addr), ntohs(client[i].addr.sin_port));
                            strcat(sendbuf, "\0");
                            public_message(client, sendbuf, client[i].connfd);

                            FD_CLR(connfd, &allset);
                            client[i].dlen = 0;
                            //free up allocated memory
                            free(client[i].name);
                            free(client[i].data);

                        } else if(!strcmp(cmd, "/name")){
                            //rename
                            args = strtok(NULL, " ");

                            if(args){
                                sprintf(sendbuf,"@> %s is renamed as %s.\r\n",
                                        client[i].name, args);
                                sprintf(client[i].name,"%s", args);
                            } else{
                                sprintf(sendbuf, "@> new name not given.\r\n");
                            }
                            echo_message(connfd, sendbuf);

                        } else if(!strcmp(cmd, "/msg")){
                            //send private message
                            args = strtok(NULL, " "); //ip

                            if(args){
                                //check if the client is online
                                temp = strtok(NULL, " "); //port
                                if(temp){
                                    if((k = is_online(args, temp, client))!= -1){

                                        //send msg to the client
                                        args = strtok(NULL, " ");
                                        if(args){
                                            sprintf(sendbuf, "\n\t\t<pvmsg> from cli[%s]:\r\n\t\t", client[i].name);
                                            while(args != NULL){
                                                strcat(sendbuf,args);
                                                strcat(sendbuf, " ");
                                                args = strtok(NULL, " ");
                                            }
                                            strcat(sendbuf, "\r\n");
                                            echo_message(client[k].connfd, sendbuf);

                                        } else{
                                            echo_message(connfd, "@> msg can't be null\r\n");
                                        }



                                    } else if(k = is_regs(args, temp, user) != -1){
                                        //keep message offline
                                        //sprintf();
                                        args = strtok(NULL, " ");
                                        if(args){
                                            //add sender name
                                            sprintf(sendbuf, "\n\t\t<pv_msg> from cli[%s]:\r\n\t\t", client[i].name);
                                            while(args != NULL){
                                                strcat(sendbuf, args);
                                                strcat(sendbuf, " ");
                                                args = strtok(NULL, " ");
                                            }
                                            strcat(sendbuf, "\r\n");
                                            if(user[k].offline_msg[0] == '\0'){
                                                sprintf(user[k].offline_msg, "%s",sendbuf);
                                            }
                                            strcat(user[k].offline_msg, sendbuf);
                                            echo_message(connfd, "@> client went offline\r\n");
                                        } else {
                                            sprintf(sendbuf, "no message is written\r\n");
                                            echo_message(connfd, sendbuf);
                                        }
                                    }
                                } else{
                                    sprintf(sendbuf, "-> {<Port> <msg>} needed\r\n");
                                    echo_message(connfd, sendbuf);
                                }

                            } else{
                                sprintf(sendbuf,"-> {<IP> <Port> <msg>} needed\r\n");
                                echo_message(connfd, sendbuf);
                            }


                        } else if(!strcmp(cmd, "/file")){
                            //share file
                            args = strtok(NULL, " "); //contains IP
                            if(args){

                                temp = strtok(NULL, " "); //port
                                if(temp){
                                    //check if the client is online
                                    if((k = is_online(args, temp,client)) != -1){
                                        //create a thread
                                        args = strtok(NULL, " ");
                                        puts("<F> file sharing...");
                                        FD_CLR(connfd, &allset); //comment
                                        sprintf(sendbuf, "/File> %s from cli[%s]\r\n", args,
                                                client[i].name);
                                        echo_message(client[k].connfd, sendbuf);

                                        param.r_fd = client[k].connfd; //receivers file deskriptor
                                        param.s_fd = connfd;
                                        sprintf(param.filename, "%s", args); //file name

                                        pthread_create(&tid, NULL, share_file, (void *)&param);
                                        pthread_join(tid, NULL);
                                        FD_SET(connfd, &allset); //comment


                                    } else{
                                        sprintf(sendbuf, "<!> Client not active\r\n");
                                        echo_message(connfd, sendbuf);
                                    }

                                } else{
                                    sprintf(sendbuf, "-> {<Port> <filename>} needed\r\n");
                                    echo_message(connfd, sendbuf);
                                }

                            } else{
                                sprintf(sendbuf, "-> {<IP> <Port> <filename>} needed\r\n");
                                echo_message(connfd, sendbuf);
                            }
                        }

                    } else {

                        //send public message
                        bzero(sendbuf, MAXDATASIZE);
                        sprintf(sendbuf, "<Pub_msg> from cli[%s]:\n\t\t", client[i].name);
                        strcat(sendbuf, recvbuf);
                        strcat(sendbuf,"\0");
                        public_message(client, sendbuf, client[i].connfd);

                    } /* end if(recvbuf[0] == '/')*/

                }

            } /*end if(FD_ISSET(connfd, &readfds))*/

        }/*end for(i=0; i <= maxi;  i++)*/

    }/* end while(1)*/
    close(sockfd);

    return 0;
}
