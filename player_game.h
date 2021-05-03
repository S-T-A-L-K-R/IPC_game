#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <semaphore.h> //sem*
#include <sys/mman.h> // mmap, munmap, shm_open, shm_unlink
#include <fcntl.h> // O_*
#include <stdlib.h> // exit
#include <unistd.h> // close, ftruncate

#include <pthread.h>

#define MAP_WIDTH 128
#define MAP_HEIGHT 128
#define SERVER_CHECK 5 //w sekundach

/////////////////////////////////////
//////////// STRUKTURY //////////////
/////////////////////////////////////
struct handshake_t
{
    sem_t client_sem_1;
    sem_t client_sem_2;
    sem_t server_sem_1;
    sem_t server_sem_2;
    char shm_name[20];
    pid_t pid_server;
    pid_t pid_client;

};
enum MOVEREQ {STOP, NORTH, WEST, EAST, SOUTH};
struct server_to_player_t
{
    sem_t sem;
    int pos_x;
    int pos_y;
    int deaths;
    int money_carried;
    int money_camped;
    int round_number;
    char map[5][5];
};
struct player_to_server_t
{
    char type;
    enum MOVEREQ move;
    int quit;
    int round_number;
};
struct player_t
{
    int id;
    int spawn_x;
    int spawn_y;
    int pos_x;
    int pos_y;
    int deaths;
    int bush;
    int money_carried;
    int money_camped;
    int fd1;
    int fd2;
    struct server_to_player_t* stp;
    struct player_to_server_t* pts;
};
struct game_t
{
    WINDOW *game_w;
    WINDOW *stats_w;
    WINDOW *getch_w;

    pthread_t pt1;
    pthread_t gpmt_pt;
    pthread_t gpgt_pt;
    pthread_t scct_pt;
    char **map;

    //Stats
    int camp_x;
    int camp_y;
    int round_number;
    int id;
    int pos_x;
    int pos_y;
    pid_t pid_server;
    pid_t pid_client;
    int deaths;

    int money_carried;
    int money_camped;

    char shm_name_stp[20];
    char shm_name_pts[20];
    int connected;
    
    int input;
    int quit;

    struct server_to_player_t* stp;
    struct player_to_server_t* pts;

    sem_t quit_sem;

};

/////////////////////////////////////
///////////// GŁÓWNE ////////////////
/////////////////////////////////////
void game_init(struct game_t* game);
void server_connect(struct game_t* game);

/////////////////////////////////////
////////////// WĄTKI ////////////////
/////////////////////////////////////
void* game_play_main_thread(void* arg);
void* game_play_getch_thread(void* arg);
void* server_connection_check_thread(void* arg);
/////////////////////////////////////
////////////// OKNA /////////////////
/////////////////////////////////////
void window_game(struct game_t* game);
void window_stats(struct game_t* game);

/////////////////////////////////////
/////////////// MAPA ////////////////
/////////////////////////////////////
void map_create(struct game_t* game);
void map_print(struct game_t* game);
void map_update(struct game_t* game);
