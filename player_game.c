#include "player_game.h"


/////////////////////////////////////
///////////// GŁÓWNE ////////////////
/////////////////////////////////////

void game_init(struct game_t* game)
{
    //Rozpoczcęcie programu
    initscr();//Tworzy ekran
    noecho();//Nie pokazują się wciskane znaki
    cbreak();//Wczytywanie znaków bez enterowania
    timeout(0);//getch() nie blokuje programu, potrafi zwrócić ERR
    curs_set(FALSE);//Brak kursora

    game->pid_client = getpid();
    map_create(game);

    game->game_w = newwin(25, 52, 0, 0);
    game->stats_w = newwin(25, 52, 0, 53);
    game->getch_w = newwin(0, 0, 0, 0);

    game->camp_x = -1;
    game->camp_y = -1;

    server_connect(game);
    
    sem_init(&game->quit_sem, 0, 0);
}
void server_connect(struct game_t* game)
{
    int fd = shm_open("IPC_handshake",  O_RDWR, 0600);
    if(fd == -1)
    {
        printf("Server not found!\n");
        game->connected = 0;
        return;
    }
    struct handshake_t* handshake = (struct handshake_t*)mmap(NULL, sizeof(struct handshake_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    sem_wait(&handshake->client_sem_1);
    game->pid_server = handshake->pid_server;
    handshake->pid_client = game->pid_client;
    sem_post(&handshake->server_sem_1);
    sem_wait(&handshake->client_sem_2);
    if(!strcmp(handshake->shm_name, "NULL"))
    {
        game->connected = 0;
        game->quit = 1;
        printf("Server full!\n");
        return;
    }
    else
    {
        game->connected = 1;
        strcpy(game->shm_name_stp, handshake->shm_name);
        strcpy(game->shm_name_pts, handshake->shm_name);
        strcat(game->shm_name_stp, "_stp");
        strcat(game->shm_name_pts, "_pts");
        int fd1 = shm_open(game->shm_name_stp, O_RDWR, 0600);
        int fd2 = shm_open(game->shm_name_pts, O_RDWR, 0600);
        game->stp = (struct server_to_player_t*)mmap(NULL, sizeof(struct server_to_player_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0);
        game->pts = (struct player_to_server_t*)mmap(NULL, sizeof(struct player_to_server_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);

        game->round_number  = game->stp->round_number;
        game->pos_x         = game->stp->pos_x;
        game->pos_y         = game->stp->pos_y;
        game->deaths        = game->stp->deaths;
        game->money_carried = game->stp->money_carried;
        game->money_camped  = game->stp->money_camped;
        game->pid_server    = handshake->pid_server;

        game->pts->move = STOP;
        game->pts->quit = 0;
        game->pts->round_number = game->round_number;
        map_update(game);
    }
    sem_post(&handshake->server_sem_2);
    munmap(handshake, sizeof(struct handshake_t));
    close(fd);
    game->connected = 1;
    return;
    
}

/////////////////////////////////////
////////////// WĄTKI ////////////////
/////////////////////////////////////
void* game_play_main_thread(void* arg)
{
    struct game_t* game = (struct game_t*) arg;
    while(!game->quit)
    {  
        if(game->quit)
        {
            break;
        }    
        sem_wait(&game->stp->sem);
        game->round_number  = game->stp->round_number;
        game->pos_x         = game->stp->pos_x;
        game->pos_y         = game->stp->pos_y;
        game->deaths        = game->stp->deaths;
        game->money_carried = game->stp->money_carried;
        game->money_camped  = game->stp->money_camped;
        
        game->pts->round_number = game->round_number;
        map_update(game);
        
        //Czyszczenie terminala
        clear();
        //Wyświetlanie na ekranach
        window_game(game);
        window_stats(game);
        //Wyświetlanie w końcu w terminalu
        wrefresh(game->game_w);
        wrefresh(game->stats_w);
        
        game->pts->move = STOP;
    }
    return NULL;
}
void* game_play_getch_thread(void* arg)
{
    struct game_t* game = (struct game_t*) arg;
    keypad(game->getch_w, TRUE);
    while(!game->quit)
    {
        game->input = wgetch(game->getch_w);
        switch(game->input)
        {
            case KEY_UP:
                game->pts->move = NORTH;
                break;
            case KEY_DOWN:
                game->pts->move = SOUTH;
                break;
            case KEY_LEFT:
                game->pts->move = WEST;
                break;
            case KEY_RIGHT:
                game->pts->move = EAST;
                break;
            case 'q':
            case 'Q':
                game->quit = 1;
                game->pts->quit = 1;
            default:
                break;
        }
    }
    return NULL;
    
}
void* server_connection_check_thread(void* arg) 
{
    struct game_t* game = (struct game_t*) arg;
    int round_check = game->stp->round_number;
    usleep(1000000 * SERVER_CHECK);
    while(!game->quit)
    {
        if(round_check != game->stp->round_number)
        {
            round_check = game->stp->round_number;
        }
        else
        {
            printf("\nServer connection lost\n");
            break;
        }
        usleep(1000000 * SERVER_CHECK);
    }   
    sem_post(&game->quit_sem);
    return NULL;
}
/////////////////////////////////////
////////////// OKNA /////////////////
/////////////////////////////////////
void window_game(struct game_t* game)
{
    map_print(game);
}
void window_stats(struct game_t* game) 
{
    mvwprintw(game->stats_w, 0, 0, "Server's PID: %lu", game->pid_server);
    if(game->camp_x == -1) mvwprintw(game->stats_w, 1, 0, " Campsite X/Y: unknown");
    else mvwprintw(game->stats_w, 1, 0, " Campsite X/Y: %d/%d     ", game->camp_x, game->camp_y);
    mvwprintw(game->stats_w,  2, 0, " Round number: %d", game->round_number);
    mvwprintw(game->stats_w,  3, 0, "Parameter:\n PID\n Curr X/Y\n Deaths\n\n Coins\n   carried\n   brought");
    mvwprintw(game->stats_w,  4, 11, "%lu",  game->pid_client);
    mvwprintw(game->stats_w,  5, 11, "%d/%d", game->pos_x, game->pos_y);
    mvwprintw(game->stats_w,  6, 11, "%d",    game->deaths);
    mvwprintw(game->stats_w,  9, 11, "%d",    game->money_carried);
    mvwprintw(game->stats_w, 10, 11, "%d",    game->money_camped);
}

/////////////////////////////////////
/////////////// MAPA ////////////////
/////////////////////////////////////
void map_create(struct game_t* game)
{
    game->map = (char**)malloc(MAP_HEIGHT * sizeof(char *));
    for (int i = 0; i < MAP_HEIGHT; i++)
    {
        *(game->map + i) = (char*)malloc(MAP_WIDTH * sizeof(char));
    }
}
void map_print(struct game_t* game)
{
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_YELLOW);
    init_pair(2, COLOR_GREEN, COLOR_YELLOW);
    init_pair(3, COLOR_WHITE, COLOR_GREEN);
    init_pair(4, COLOR_WHITE, COLOR_BLUE);
    init_pair(5, COLOR_RED, COLOR_BLACK);
    for(int i = 0;i<25;i++)
    {
        for (int j = 0;j<52;j++)
        {
            switch(game->map[i][j])
            {
                //Ściana
                case 'X':
                    mvwaddch(game->game_w, i, j, ACS_CKBOARD);
                    break;

                    //Hajsy
                case 'c':
                case 'C':
                case 'T':
                    wattron(game->game_w, COLOR_PAIR(1));
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    wattroff(game->game_w, COLOR_PAIR(1));
                    break;

                    //Upuszczone hajsy
                case 'D':
                    wattron(game->game_w, COLOR_PAIR(2));
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    wattroff(game->game_w, COLOR_PAIR(2));
                    break;

                    //Obóz
                case 'A':
                    wattron(game->game_w, COLOR_PAIR(3));
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    wattroff(game->game_w, COLOR_PAIR(3));
                    break;

                    //Gracze
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    wattron(game->game_w, COLOR_PAIR(4));
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    wattroff(game->game_w, COLOR_PAIR(4));
                    break;

                    //Bestia
                case '*':
                    wattron(game->game_w, COLOR_PAIR(5));
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    wattroff(game->game_w, COLOR_PAIR(5));
                    break;

                    //Ściany, korytarze i krzaki
                default:
                    mvwprintw(game->game_w, i, j, "%c", game->map[i][j]);
                    break;
            }
        }
    }

}
void map_update(struct game_t* game)
{
    for(int i = 0; i < MAP_HEIGHT; i++)
    {
        for(int j = 0; j < MAP_WIDTH; j++)
        {
            if(game->map[i][j] != 'X' && game->map[i][j] != '#')
            {
                game->map[i][j] = ' ';
            }
        }
    }
    for(int i = -2; i < 3; i++)
    {
        for(int j = -2; j < 3; j++)
        {
            if((game->pos_y + i >= 0 )&& (game->pos_y + i < MAP_HEIGHT) && (game->pos_x + j >= 0) && (game->pos_x + j < MAP_WIDTH))
            {
                game->map[i + game->pos_y][j + game->pos_x] = game->stp->map[i + 2][j + 2];
                if(game->camp_x == -1 && game->camp_y == -1 && game->map[i + game->pos_y][j + game->pos_x] == 'A')
                {
                    game->camp_x = j + game->pos_x;
                    game->camp_y = i + game->pos_y;
                }
            }
        }
    }
}

