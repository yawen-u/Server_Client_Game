#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

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

#define MAXLINE 256

// RIO ---------------------------------------------------------------------------

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


// Dimensions for the drawn grid (should be GRIDSIZE * texture dimensions)
#define GRID_DRAW_WIDTH 640
#define GRID_DRAW_HEIGHT 640

#define WINDOW_WIDTH GRID_DRAW_WIDTH
#define WINDOW_HEIGHT (HEADER_HEIGHT + GRID_DRAW_HEIGHT)

// Header displays current score
#define HEADER_HEIGHT 50

// Number of cells vertically/horizontally in the grid
#define GRIDSIZE 10

#define MAX_PLAYERS 4

typedef struct sockaddr SA;

typedef struct
{
    int x;
    int y;
} Position;

typedef enum
{
    TILE_GRASS,
    TILE_TOMATO
} TILETYPE;

TILETYPE grid[GRIDSIZE][GRIDSIZE];

Position playerPosition;
Position lastPosition;

int id_array[MAX_PLAYERS];
Position position_array[MAX_PLAYERS];

int score;
int level;
int numTomatoes;
int numPlayers;

bool shouldExit = false;

TTF_Font* font;

// get a random value in the range [0, 1]
double rand01()
{
    return (double) rand() / (double) RAND_MAX;
}

void initGrid()
{
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            double r = rand01();
            if (r < 0.1) {
                grid[i][j] = TILE_TOMATO;
                numTomatoes++;
            }
            else
                grid[i][j] = TILE_GRASS;
        }
    }

    // force player's position to be grass
    if (grid[playerPosition.x][playerPosition.y] == TILE_TOMATO) {
        grid[playerPosition.x][playerPosition.y] = TILE_GRASS;
        numTomatoes--;
    }

    // force others to be on grass too
    for (int i = 0; i < MAX_PLAYERS; i++){

        if (id_array[i] != 0){

            if (grid[position_array[i].x][position_array[i].y] == TILE_TOMATO){
                grid[position_array[i].x][position_array[i].y] = TILE_GRASS;
                numTomatoes--;
            }
        }
    }

    // ensure grid isn't empty
    while (numTomatoes == 0)
        initGrid();
}

void initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Error initializing SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    int rv = IMG_Init(IMG_INIT_PNG);
    if ((rv & IMG_INIT_PNG) != IMG_INIT_PNG) {
        fprintf(stderr, "Error initializing IMG: %s\n", IMG_GetError());
        exit(EXIT_FAILURE);
    }

    if (TTF_Init() == -1) {
        fprintf(stderr, "Error initializing TTF: %s\n", TTF_GetError());
        exit(EXIT_FAILURE);
    }
}

void moveTo(int x, int y)
{
    // Prevent falling off the grid
    if (x < 0 || x >= GRIDSIZE || y < 0 || y >= GRIDSIZE)
        return;

    // Prevent to go on top of other players
    for (int i = 0; i < MAX_PLAYERS; i++){

        if (id_array[i] != 0){

            if ( (x == position_array[i].x) && (y == position_array[i].y) ){
                return;
            }
        }
    }

    // Sanity check: player can only move to 4 adjacent squares
    if (!(abs(playerPosition.x - x) == 1 && abs(playerPosition.y - y) == 0) &&
        !(abs(playerPosition.x - x) == 0 && abs(playerPosition.y - y) == 1)) {
        fprintf(stderr, "Invalid move attempted from (%d, %d) to (%d, %d)\n", playerPosition.x, playerPosition.y, x, y);
        return;
    }

    playerPosition.x = x;
    playerPosition.y = y;

    if (grid[x][y] == TILE_TOMATO) {
        grid[x][y] = TILE_GRASS;
        score++;
        numTomatoes--;
        if (numTomatoes == 0) {
            level++;
            initGrid();
        }
    }
}

void handleKeyDown(SDL_KeyboardEvent* event)
{
    // ignore repeat events if key is held down
    if (event->repeat)
        return;

    if (event->keysym.scancode == SDL_SCANCODE_Q || event->keysym.scancode == SDL_SCANCODE_ESCAPE)
        shouldExit = true;

    if (event->keysym.scancode == SDL_SCANCODE_UP || event->keysym.scancode == SDL_SCANCODE_W)
        moveTo(playerPosition.x, playerPosition.y - 1);

    if (event->keysym.scancode == SDL_SCANCODE_DOWN || event->keysym.scancode == SDL_SCANCODE_S)
        moveTo(playerPosition.x, playerPosition.y + 1);

    if (event->keysym.scancode == SDL_SCANCODE_LEFT || event->keysym.scancode == SDL_SCANCODE_A)
        moveTo(playerPosition.x - 1, playerPosition.y);

    if (event->keysym.scancode == SDL_SCANCODE_RIGHT || event->keysym.scancode == SDL_SCANCODE_D)
        moveTo(playerPosition.x + 1, playerPosition.y);
}

void processInputs()
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                shouldExit = true;
                break;

            case SDL_KEYDOWN:
                handleKeyDown(&event.key);
                break;

            default:
                break;
        }
    }
}

void drawGrid(SDL_Renderer* renderer, SDL_Texture* grassTexture, SDL_Texture* tomatoTexture, SDL_Texture* playerTexture)
{
    SDL_Rect dest;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            dest.x = 64 * i;
            dest.y = 64 * j + HEADER_HEIGHT;
            SDL_Texture* texture = (grid[i][j] == TILE_GRASS) ? grassTexture : tomatoTexture;
            SDL_QueryTexture(texture, NULL, NULL, &dest.w, &dest.h);
            SDL_RenderCopy(renderer, texture, NULL, &dest);
        }
    }

    dest.x = 64 * playerPosition.x;
    dest.y = 64 * playerPosition.y + HEADER_HEIGHT;
    SDL_QueryTexture(playerTexture, NULL, NULL, &dest.w, &dest.h);
    SDL_RenderCopy(renderer, playerTexture, NULL, &dest);
}

void drawOtherPlayers(SDL_Renderer* renderer, SDL_Texture* playerTexture){

    SDL_Rect dest;
    for (int i = 0; i < MAX_PLAYERS; i++){

        if (id_array[i] != 0){

            dest.x = 64 * position_array[i].x;
            dest.y = 64 * position_array[i].y + HEADER_HEIGHT;
            SDL_QueryTexture(playerTexture, NULL, NULL, &dest.w, &dest.h);
            SDL_RenderCopy(renderer, playerTexture, NULL, &dest);
        }
    }

}

void drawUI(SDL_Renderer* renderer)
{
    // largest score/level supported is 2147483647
    char scoreStr[18];
    char levelStr[18];
    sprintf(scoreStr, "Score: %d", score);
    sprintf(levelStr, "Level: %d", level);

    SDL_Color white = {255, 255, 255};
    SDL_Surface* scoreSurface = TTF_RenderText_Solid(font, scoreStr, white);
    SDL_Texture* scoreTexture = SDL_CreateTextureFromSurface(renderer, scoreSurface);

    SDL_Surface* levelSurface = TTF_RenderText_Solid(font, levelStr, white);
    SDL_Texture* levelTexture = SDL_CreateTextureFromSurface(renderer, levelSurface);

    SDL_Rect scoreDest;
    TTF_SizeText(font, scoreStr, &scoreDest.w, &scoreDest.h);
    scoreDest.x = 0;
    scoreDest.y = 0;

    SDL_Rect levelDest;
    TTF_SizeText(font, levelStr, &levelDest.w, &levelDest.h);
    levelDest.x = GRID_DRAW_WIDTH - levelDest.w;
    levelDest.y = 0;

    SDL_RenderCopy(renderer, scoreTexture, NULL, &scoreDest);
    SDL_RenderCopy(renderer, levelTexture, NULL, &levelDest);

    SDL_FreeSurface(scoreSurface);
    SDL_DestroyTexture(scoreTexture);

    SDL_FreeSurface(levelSurface);
    SDL_DestroyTexture(levelTexture);
}

// Additional Helper Functions
int open_clientfd(char *hostname, char *port){

    int clientfd;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_socktype = SOCK_STREAM; /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV; /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG; /* Recommended for connections */

    getaddrinfo(hostname, port, &hints, &listp);

    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {

        /* Create a socket descriptor */
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0){
            continue; /* Socket failed, try the next */
        }

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1){
            break; /* Success */
        }

        close(clientfd); /* Connect failed, try another */
    }

    /* Clean up */
    freeaddrinfo(listp);

    /* All connects failed */
    if (!p){
        return -1;
    }

    /* The last connect succeeded */
    else{
        return clientfd;
    }
}

void* send_data(void* arg) {

    int clientfd = *((int *) arg);
    //char buf[127];

    while (!shouldExit && clientfd != -1) {

        if ((lastPosition.x != playerPosition.x) || (lastPosition.y != playerPosition.y)) {

            char* buf = malloc(127 * sizeof(char));
            int buf_index = 0;

            // index 0-3 is x-position
            int x = playerPosition.x;
            for (int i = 3; i >= 0; i--) {    
                int X = x >> i;
                if ((X & 1) > 0)
                    buf[buf_index++] = '1';
                else
                    buf[buf_index++] = '0';
            }

            // index 4-7 is y-position
            int y = playerPosition.y;
            for (int i = 3; i >= 0; i--) {    
                int Y = y >> i;
                if ((Y & 1) > 0)
                    buf[buf_index++] = '1';
                else
                    buf[buf_index++] = '0';
            }

            // index 8-17 is Score
            int s = score; 
            for (int i = 9; i >= 0; i--) {    
                int S = s >> i;
                if ((S & 1) > 0)
                    buf[buf_index++] = '1';
                else
                    buf[buf_index++] = '0';
            }

            // index 18-25 is Level
            int l = level; 
            for (int i = 7; i >= 0; i--) {    
                int L = l >> i;
                if ((L & 1) > 0)
                    buf[buf_index++] = '1';
                else
                    buf[buf_index++] = '0';
            }

            // index 26-125 is Grid
            for (int i = 0; i < GRIDSIZE; i++) {
                for (int j = 0; j < GRIDSIZE; j++) {
                    if (grid[j][i] == TILE_GRASS) {
                        buf[buf_index++] = '0';
                    } else if (grid[j][i] == TILE_TOMATO){
                        buf[buf_index++] = '1';
                    }
                }    
            }
            //printf("client send:\n%s\n", buf);

            // Update last position
            lastPosition.x = playerPosition.x;
            lastPosition.y = playerPosition.y;

            rio_writen(clientfd, buf, strlen(buf));
            free(buf);
        }
    }

    //shouldExit = true;
    return NULL;
}

void* receive_data(void* arg){

    rio_t rio = *((rio_t *) arg);
    size_t n;
    char receive_buf[131];

    while ((n = rio_readlineb(&rio, receive_buf, 131)) != 0) {
        
        // X-postion
        int i, k = 0;
        for (i = 0; i < 4; i++) {
            k = 10 * k + (receive_buf[i] - '0');
        }
        int rem, dec = 0, b = 0;
        while (k != 0) {
            rem = k % 10;
            k /= 10;
            dec += rem * pow(2, b);
            b++;
        }
        int X = dec;
        //printf("X: %d\n", X);


        // Y-position
        k = 0;
        for (i = 4; i < 8; i++) {
            k = 10 * k + (receive_buf[i] - '0');
        }
        dec = 0;
        b = 0;
        while (k != 0) {
            rem = k % 10;
            k /= 10;
            dec += rem * pow(2, b);
            b++;
        }
        int Y = dec;
        //printf("Y: %d\n", Y);


        // Score
        k = 0;
        for (i = 8; i < 18; i++) {
            k = 10 * k + (receive_buf[i] - '0');
        }
        dec = 0;
        b = 0;
        while (k != 0) {
            rem = k % 10;
            k /= 10;
            dec += rem * pow(2, b);
            b++;
        }
        int S = dec;
        //printf("Score: %d\n", S);
        score = S;


        // Level
        k = 0;
        for (i = 18; i < 26; i++) {
            k = 10 * k + (receive_buf[i] - '0');
        }
        dec = 0;
        b = 0;
        while (k != 0) {
            rem = k % 10;
            k /= 10;
            dec += rem * pow(2, b);
            b++;
        }
        int L = dec;
        //printf("Level: %d\n", L);
        level = L;


        // Grid
        int buf_i = 26;
        for (int i = 0; i < GRIDSIZE; i++) {
            for (int j = 0; j < GRIDSIZE; j++) {
                if (receive_buf[buf_i] == '1') {
                    grid[j][i] = TILE_TOMATO;
                } else {
                    grid[j][i] = TILE_GRASS;
                }
                buf_i++;
            }    
        }

        // Recount how many tomatoes we have now and update
        int temp_tomatoes = 0;
        for (int i = 0; i < GRIDSIZE; i++){
            for (int j = 0; j < GRIDSIZE; j++){

                if (grid[i][j] == TILE_TOMATO){
                    temp_tomatoes++;
                }
            }
        }
        numTomatoes = temp_tomatoes;
    
    // # Players
        k = 0;
        for (i = 126; i < 130; i++) {
            k = 10 * k + (receive_buf[i] - '0');
        }
        dec = 0;
        b = 0;
        while (k != 0) {
            rem = k % 10;
            k /= 10;
            dec += rem * pow(2, b);
            b++;
        }
        numPlayers = dec;
        printf("num_players: %d\n", numPlayers);

        // Update our array of player positions
        id_array[numPlayers - 1] = numPlayers;
        position_array[numPlayers - 1].x = X;
        position_array[numPlayers - 1].y = Y;
    }

    return NULL;
}

//----------------------------------------------------------------------------
// RIO functions
//----------------------------------------------------------------------------

ssize_t rio_readn(int fd, void *usrbuf, size_t n){

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
    return (n - nleft); /* Return >= 0 */
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n){

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

void rio_readinitb(rio_t *rp, int fd){

    rp->rio_fd = fd;
    rp->rio_cnt = 0;
    rp->rio_bufptr = rp->rio_buf;
}

static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n){

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

ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen){

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
            } else {
                break; /* EOF, some data was read */
            }
        } else {
            return -1; /* Error */
        }
    }

    *bufp = 0;
    return n-1;
}

ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n){

    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;

    while (nleft > 0) {

        if ((nread = rio_read(rp, bufp, nleft)) < 0){
            return -1; /* errno set by read() */
        }

        else if (nread == 0){
            break; /* EOF */
        }

        nleft -= nread;
        bufp += nread;
    }
    return (n - nleft); /* Return >= 0 */
}

//_________________________________________________________________________________

int main(int argc, char* argv[]) {

    // Connection with server setup
    int clientfd;
    int* clientfd_ptr = &clientfd;
    
    char *host, *port;
    
    rio_t rio;
    rio_t* rio_ptr = &rio;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host = argv[1];
    port = argv[2];

    clientfd = open_clientfd(host, port);
    if (clientfd == -1) {
        fprintf(stderr, "connection error: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    rio_readinitb(&rio, clientfd);


    // Game setup
    for (int i = 0; i < MAX_PLAYERS; i++){

        id_array[i] = 0;
        position_array[i].x = 0;
        position_array[i].y = 0;
    }

    srand(time(NULL));

    level = 1;

    initSDL();

    font = TTF_OpenFont("resources/Burbank-Big-Condensed-Bold-Font.otf", HEADER_HEIGHT);
    if (font == NULL) {
        fprintf(stderr, "Error loading font: %s\n", TTF_GetError());
        exit(EXIT_FAILURE);
    }

    playerPosition.x = playerPosition.y = GRIDSIZE / 2;
    initGrid();

    SDL_Window* window = SDL_CreateWindow("Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);

    if (window == NULL) {
        fprintf(stderr, "Error creating app window: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);

    if (renderer == NULL)
    {
        fprintf(stderr, "Error creating renderer: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Texture *grassTexture = IMG_LoadTexture(renderer, "resources/grass.png");
    SDL_Texture *tomatoTexture = IMG_LoadTexture(renderer, "resources/tomato.png");
    SDL_Texture *playerTexture = IMG_LoadTexture(renderer, "resources/player.png");

    
    // main game loop
    pthread_t send_thread;
    pthread_t receive_thread;

    pthread_create(&send_thread, NULL,(void *) send_data, clientfd_ptr);

    pthread_create(&receive_thread, NULL,(void *) receive_data, rio_ptr);

    while (!shouldExit) {
        SDL_SetRenderDrawColor(renderer, 0, 105, 6, 255);
        SDL_RenderClear(renderer);

        lastPosition = playerPosition;
        processInputs();

        drawGrid(renderer, grassTexture, tomatoTexture, playerTexture);
        drawOtherPlayers(renderer, playerTexture);
        drawUI(renderer);

        SDL_RenderPresent(renderer);

        SDL_Delay(16); // 16 ms delay to limit display to 60 fps
    }

    close(clientfd);

    // clean up everything
    SDL_DestroyTexture(grassTexture);
    SDL_DestroyTexture(tomatoTexture);
    SDL_DestroyTexture(playerTexture);

    TTF_CloseFont(font);
    TTF_Quit();

    IMG_Quit();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}