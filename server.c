#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>

typedef struct sockaddr SA;
#define MAXLINE 256

// Client -----------------------------------------------------------------------------------
#define LISTENQ 16

typedef struct {
    struct sockaddr_storage clientaddr;
    int clientfd;
    int id;
    char* data;
} client_t;

client_t* clients[LISTENQ];
static int id = 1;

// RIO -------------------------------------------------------------------------------------
#define RIO_BUFSIZE 8192
typedef struct {
    int rio_fd;                /* Descriptor for this internal buf */
    int rio_cnt;               /* Unread bytes in internal buf */
    char *rio_bufptr;          /* Next unread byte in internal buf */
    char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

/* Rio (Robust I/O) package */
ssize_t rio_readn(int fd, void *usrbuf, size_t n);
ssize_t rio_writen(int fd, void *usrbuf, size_t n);
void rio_readinitb(rio_t *rp, int fd); 
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);

//________________________________________________________________________________

pthread_t tid;

void retransmit(int id) 
{   

    // send the number of players  to the current client
    client_t* curr = clients[id-1];
    char num_players[4];
    int index = 0;
    for (int i = 3; i >= 0; i--) {    
        int N = id >> i;
        if ((N & 1) > 0)
            num_players[index++] = '1';
        else
            num_players[index++] = '0';
    }

    printf("num_players array: ");
    for (int i = 0; i < 4; i++) {
        printf("%c", num_players[i]);
    }
    printf("\n");
    
    strcat(curr->data, num_players);
    //rio_writen(curr->clientfd, curr->data, strlen(curr->data));

    // send other players' postiotions
    for (int i = 0; i < LISTENQ; i++) {

        // Check if the current client is valid
        if (clients[i] != NULL) {

            if (clients[i]->id != id) {
                rio_writen(clients[i]->clientfd, curr->data, strlen(curr->data));
            }

        }
    }
}

void echo(client_t* current_client)
{   
    int connfd = current_client->clientfd;
    size_t n;
    char buff[127];
    rio_t rio;
    rio_readinitb(&rio, connfd);
    while ((n = rio_readlineb(&rio, buff, 127)) != 0) {
        //printf("server received %s\n", buff);
        current_client->data = malloc(131* sizeof(char));
        strcpy(current_client->data, buff);

        for (int i = 0; i < 4; i++) {
            printf("%c", buff[i]);
        }
        printf(" | ");
        for (int i = 4; i < 8; i++) {
            printf("%c", buff[i]);
        }
        printf(" | ");
        for (int i = 8; i < 18; i++) {
            printf("%c", buff[i]);
        }
        printf(" | ");
        for (int i = 18; i < 26; i++) {
            printf("%c", buff[i]);
        }
    
        for (int i = 26; i < 127; i++) {
            if (((i - 26) % 10) == 0) {
                printf("\n");
            }
            printf("%c", buff[i]);
        }
        printf("\n");

        retransmit(current_client->id);
        free(current_client->data);
    }
}

int open_listenfd(int port)
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    
    /* Eliminates "Address already in use" error from bind */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int)) < 0) {
        return -1;
    }

    /* Listenfd will be an endpoint for all requests to port on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0) {
        return -1;
    }

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, 1024) < 0) {
        return -1;
    }
    return listenfd;
}

void* thread(void* vargp)
{
    client_t* current_client = (client_t *) vargp;
    pthread_detach(pthread_self());
    // free(vargp);
    echo(current_client);
    close(current_client->clientfd);
    return NULL;
}

void print_addr(struct sockaddr* addr){

    struct sockaddr_in* addr_in = (struct sockaddr_in *) addr;

    char *ip = inet_ntoa(addr_in->sin_addr);

    printf("%s\n", ip);
}

// Helper function to add client into our array of clients
void add_client(client_t* current_client){

    for (int i = 0; i < LISTENQ; i++){

        if (clients[i] == NULL){
            clients[i] = current_client;
            break;
        }
    }
}

// Helper function to remove client from our array of clients
void remove_client(int id){

    for (int i = 0; i < LISTENQ; i++){

        // Check if the current client is valid
        if (clients[i] != NULL){

            // We found it
            if (clients[i]->id == id){

                free(clients[i]);
                clients[i] = NULL;
                break;
            }
        }
    }
}

int main(int argc, char **argv) 
{
    int listenfd, *connfdp;
    socklen_t client_len;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "%s - invalid port\n", argv[1]);
        exit(0);
    }

    listenfd = open_listenfd(atoi(argv[1]));

    while (1) {
        client_len = sizeof(struct sockaddr_storage);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *) &clientaddr, &client_len);

        // Create a client profile for the new connection
        client_t* current_client = (client_t*) malloc(sizeof(client_t));
        current_client->clientaddr = clientaddr;
        current_client->clientfd = *connfdp;
        current_client->id = id;
        id++;

        // Add client profile into our array
        add_client(current_client);

        // print_addr( (struct sockaddr *) &clientaddr);
        for (int i = 0; i < LISTENQ; i++) {
            if (clients[i] != NULL) {
                printf("Client: %d\nIP: ", clients[i]->id);
                print_addr( (SA*) & (clients[i]->clientaddr));
            }
        }

        pthread_create(&tid, NULL, thread, (void *) current_client);
    }

    return 0;
}

// RIO Package _____________________________________________________________________________

ssize_t rio_readn(int fd, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {

        if ((nread = read(fd, bufp, nleft)) < 0) {

            /* Interrupted by sig handler return */
            if (errno == EINTR){
                nread = 0; /* and call read() again */
            }

            else{
                return -1; /* errno set by read() */
            }
        }

        else if (nread == 0){
            break; /* EOF */
        }
        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft); /* Return >= 0 */       /* return >= 0 */
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;

    while (nleft > 0) {

        if ((nwritten = write(fd, bufp, nleft)) <= 0) {

            /* Interrupted by sig handler return */
            if (errno == EINTR){
                nwritten = 0; /* and call write() again */
            }

            else{
                return -1; /* errno set by write() */
            }
        }

        nleft -= nwritten;
        bufp += nwritten;
    }
    return n;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    /* Refill if buf is empty */
    while (rp->rio_cnt <= 0) { 

        rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, sizeof(rp->rio_buf));

        if (rp->rio_cnt < 0) {
            /* Interrupted by sig handler return */
            if (errno != EINTR){
                return -1;
            }
        }

        /* EOF */
        else if (rp->rio_cnt == 0){
            return 0;
        }

        else{
            rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
        }
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;

    if (rp->rio_cnt < n){
        cnt = rp->rio_cnt;
    }
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}

void rio_readinitb(rio_t *rp, int fd) 
{
    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {
        if ((nread = rio_read(rp, bufp, nleft)) < 0){
            return -1; /* errno set by read() */
        } else if (nread == 0) {
            break; /* EOF */
        }

        nleft -= nread;
        bufp += nread;
    }

    return (n - nleft); /* Return >= 0 */
}

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    int n, rc;
    char c, *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) {

        if ((rc = rio_read(rp, &c, 1)) == 1) {

            *bufp++ = c;
            if (c == '\n') {
                n++;
                break;
            }
        } else if (rc == 0) {
            if (n == 1){
                return 0; /* EOF, no data read */
            }

            else{
                break; /* EOF, some data was read */
            }
        } else{
            return -1; /* Error */
        }
    }

    *bufp = 0;
    return n-1;
}
