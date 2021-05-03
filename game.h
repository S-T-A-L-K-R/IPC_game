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

#include <vector>

#include <errno.h>

#define MAP_WIDTH 51
#define MAP_HEIGHT 25
#define MAP_FILENAME "map.txt"
#define BEAST_MAX 4

using namespace std;

/////////////////////////////////////
//////////// STRUKTURY //////////////
/////////////////////////////////////

struct handshake_t //Struktura odpowiadająca za połączenia gracza z serwerem
{
    //#1. Poprawne połączenie
    //Pierwszy semafor klienta serwer zostawia odblokowany, klient blokuje pierwszy semafor klienta, daje request w zmiennej, (może nie request, tylko po prostu semafor odblokowywuje)
    //      odblokowuje pierwszy semafor serwera i stoi na drugim semaforze klienta
    //Serwer przechodzi przez pierwszy semafor serwera, odpowiada w drugiej zmiennej dając nazwę shm'a, do shma wysyła dane gry(pośród nich jest ID, nie trzeba dawać zmiennej po to),
    //      odblokowuje drugi semafor klienta i stoi na drugim semaforze serwera
    //Klient przechodzi przez drugi semafor klienta, zczytuje shm z danymi gry, odblokowuje drugi semafor serwera i idzie prosto do gry
    //Serwer przechodzi przez drugi semafor serwera, czyści pamięć handshake'a,
    //      blokuje drugi semafor klienta, blokuje drugi semafor serwera,
    //      odblokowuje pierwszy semafor klienta, blokuje pierwszy semafor serwera i stoi na nim 

    //#2. Serwer pełny
    //Pierwszy semafor klienta serwer zostawia odblokowany, klient blokuje pierwszy semafor klienta, daje request w zmiennej, (może nie request, tylko po prostu semafor odblokowywuje)
    //      odblokowuje pierwszy semafor serwera i stoi na drugim semaforze klienta
    //Serwer przechodzi przez pierwszy semafor serwera, inkrementuje zmienną, albo w nazwie shm'a wysyła "NULL"
    //      odblokowuje drugi semafor klienta i stoi na drugim semaforze serwera
    //Klient przechodzi przez drugi semafor klienta, widzi NULL, odblokowywuje drugi semafor serwera i idzie się wyłączyć
    //Serwer przechodzi przez drugi semafor serwera, czyści pamięć handshake'a,
    //      blokuje drugi semafor klienta, blokuje drugi semafor serwera,
    //      odblokowuje pierwszy semafor klienta, blokuje pierwszy semafor serwera i stoi na nim 

    sem_t client_sem_1;
    sem_t client_sem_2;
    sem_t server_sem_1;
    sem_t server_sem_2;
    char shm_name[20];
    pid_t pid_server;
    pid_t pid_client;

};
struct interaction_t
{
    int x;
    int y;
    int value;
    char display;
};
typedef struct interaction_t inter;
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
    pid_t pid_player;
    char id;
    int spawn_x;
    int spawn_y;
    int pos_x;
    int pos_y;
    int deaths;
    int bush;
    int money_carried;
    int money_camped;
    char shm_name[20];
    char shm_name_pts[20];
    char shm_name_stp[20];
    int fd1;
    int fd2;
    struct server_to_player_t* stp;
    struct player_to_server_t* pts;
};
struct beast_t
{
    int id;
    int pos_x;
    int pos_y;
    int bush;
    int status;
    MOVEREQ move;
    struct game_t* game;
    char sem_name[20];
    sem_t* sem;
};
struct game_t
{
    WINDOW *game_w;
    WINDOW *stats_w;

    pthread_t plt_pt;
    pthread_t gmt_pt;

    pid_t pid_server;

    char **map;
    char **map_default;

    vector < inter > gold;

    inter camp;

    vector < struct player_t > players;
    int player_avatar[4];
    vector < struct beast_t > beasts;
    pthread_t beast_pt[BEAST_MAX];
    int beast_count;
    int round_number;
    int input;
    int quit;

    int fd;
    struct handshake_t *handshake;


};


/////////////////////////////////////
///////////// GŁÓWNE ////////////////
/////////////////////////////////////
void game_init(struct game_t* game);
void game_stop(struct game_t* game);

/////////////////////////////////////
////////////// WĄTKI ////////////////
/////////////////////////////////////
void* game_main_thread(void* arg);
void* player_listener_thread(void* arg);

/////////////////////////////////////
///////////// GRACZE ////////////////
/////////////////////////////////////
void player_init(struct game_t* game);
int player_kill(struct game_t* game, struct player_t* player);

void players_download_data(struct game_t* game);
void players_upload_data(struct game_t* game);


/////////////////////////////////////
///////////// BESTIE //////////////// 
/////////////////////////////////////
void beast_add(struct game_t* game);
void* beast_thread(void* arg);
void* beast_thread_v2(void* arg);
void* beast_thread_v3(void* arg);
void* beast_thread_v4(void* arg);
int check_sight(const int x1, const int y1, const int x2, const int y2, struct game_t* game); // aka BresenhamLine - Prosto z Wikipedii
MOVEREQ path_req(struct game_t* game, MOVEREQ prev, int my_x, int my_y, int search_x, int search_y, int step);
void beasts_download_data(struct game_t* game);


/////////////////////////////////////
////////////// OKNA /////////////////
/////////////////////////////////////
void window_game(struct game_t* game);
void map_print(struct game_t* game);
void window_stats(struct game_t* game) ;

/////////////////////////////////////
///////// MAPA I OBIEKTY ////////////
/////////////////////////////////////
void map_create(struct game_t* game);
void map_clear(struct game_t* game);
int gold_add_rand(struct game_t* game, int value, char display);
int gold_add(struct game_t* game, int value, char display, int x, int y);
void gold_update(struct game_t* game);
void camp_generate(struct game_t* game);
void camp_update(struct game_t* game);
void players_update(struct game_t* game);
void beasts_update(struct game_t* game);