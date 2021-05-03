#include "game.h"


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
    keypad(stdscr, TRUE);
    curs_set(FALSE);//Brak kursora

    game->round_number = 0;
    game->input = 0;
    game->pid_server = getpid();
    map_create(game);

    game->game_w = newwin(MAP_HEIGHT, MAP_WIDTH, 0, 0);
    game->stats_w = newwin(25, 52, 0, 53);

    camp_generate(game);
    camp_update(game);
    if(strcmp(MAP_FILENAME, "map3.txt"))
    {
        for(int i = 0; i < 10; i++) gold_add_rand(game, 1 , 'c');
        for(int i = 0; i < 5; i++)  gold_add_rand(game, 10, 'C');
        for(int i = 0; i < 1; i++)  gold_add_rand(game, 50, 'T');
        gold_update(game);
    }
    
    for(int i = 0; i < 4; i++) game->player_avatar[i] = 0;
    
    game->round_number = 0;
    game->input = '\0';
    game->quit = 0;
    game->beasts.resize(BEAST_MAX);
    game->beast_count = 0;
}
void game_stop(struct game_t* game)
{
    endwin();
    sem_post(&game->handshake->server_sem_1);
    // pthread_cancel(game->plt_pt);
    pthread_join(game->plt_pt, NULL);
    for(int i = 0; i < (int)game->players.size(); i++)
    {
        player_kill(game, &game->players[i]);
    }
    for(int i = 0; i < game->beast_count; i++)
    {
        //sem_close(game->beasts[i].sem);
        sem_unlink(game->beasts[i].sem_name);
        pthread_cancel(game->beast_pt[i]);
    }
    free(game);
    
}

/////////////////////////////////////
////////////// WĄTKI ////////////////
/////////////////////////////////////
void* game_main_thread(void* arg)
{
    struct game_t* game = (struct game_t*) arg;
    while(!game->quit)
    {
        //Czyszczenie terminala
        clear();
        //Reset mapy, potem obiekty ją zaktualizują sobie
        map_clear(game);

        game->round_number++;
        players_download_data(game);
        beasts_download_data(game);
        gold_update(game);
        camp_update(game);
        beasts_update(game);
        players_update(game);
        players_upload_data(game);
        
        //Wyświetlanie na ekranach
        window_game(game);
        window_stats(game);

        //Wyświetlanie w końcu w terminalu
        wrefresh(game->game_w);
        wrefresh(game->stats_w);
        usleep(1000000);

        game->input = getch();

        switch(game->input)
        {
            case 'c':
                gold_add_rand(game, 1 , 'c');
                break;

            case 'C':
                gold_add_rand(game, 10, 'C');
                break;

            case 'T':
                gold_add_rand(game, 50, 'T');
                break;

            case 'q':
            case 'Q':
                game->quit = 1;
                break;

            case 'b':
            case 'B':
                beast_add(game);
                break;

            default:
                break;

        }

    }
    return NULL;
}
void* player_listener_thread(void* arg)
{
    struct game_t *game = (struct game_t*)arg;
    game->fd = shm_open("IPC_handshake", O_CREAT | O_RDWR, 0600);
    ftruncate(game->fd, sizeof(struct handshake_t));
    game->handshake = (struct handshake_t*)mmap(NULL, sizeof(struct handshake_t), PROT_READ | PROT_WRITE, MAP_SHARED, game->fd, 0);
    sem_init(&game->handshake->client_sem_1, 1, 1);
    sem_init(&game->handshake->client_sem_2, 1, 0);
    sem_init(&game->handshake->server_sem_1, 1, 0);
    sem_init(&game->handshake->server_sem_2, 1, 0);
    game->handshake->pid_server = game->pid_server;
    while(1)
    {
        sem_wait(&game->handshake->server_sem_1);
        if(game->quit) break;
        if(game->players.size() >= 4)
        {
            strcpy(game->handshake->shm_name, "NULL");
            sem_post(&game->handshake->client_sem_2);
            sem_wait(&game->handshake->server_sem_2);
        }
        else
        {
            
            player_init(game);
            strcpy(game->handshake->shm_name,game->players.back().shm_name);
            game->players.back().pid_player = game->handshake->pid_client;
            sem_post(&game->handshake->client_sem_2);
            sem_wait(&game->handshake->server_sem_2);
        }
        sem_post(&game->handshake->client_sem_1);
    }
    sem_destroy(&game->handshake->server_sem_1);
    sem_destroy(&game->handshake->server_sem_2);
    sem_destroy(&game->handshake->client_sem_1);
    sem_destroy(&game->handshake->client_sem_2);
    munmap(game->handshake, sizeof(struct handshake_t));
    close(game->fd);
    shm_unlink("IPC_handshake");
    return NULL;
}

/////////////////////////////////////
///////////// GRACZE ////////////////
/////////////////////////////////////
void player_init(struct game_t* game)
{
    struct player_t player;
    
    /////////////
    //// SHM ////
    /////////////

    char buffer;
    
    for(int i = 0; i < 4; i++)
    {
        if(game->player_avatar[i] == 0)
        {
            game->player_avatar[i] = 1;
            buffer = (char)i + 1;
            player.id = (char)i + 1;           
            break;
        }
    }
    
    strcpy(player.shm_name, "IPC_Client");
    //strcat(player.shm_name, &buffer);
    player.shm_name[10] = buffer;
    player.shm_name[11] = '\0';

    strcpy(player.shm_name_stp, player.shm_name);
    strcat(player.shm_name_stp, "_stp");

    strcpy(player.shm_name_pts, player.shm_name);
    strcat(player.shm_name_pts, "_pts");


    player.fd1 = shm_open(player.shm_name_stp, O_CREAT | O_RDWR, 0600);
    player.fd2 = shm_open(player.shm_name_pts, O_CREAT | O_RDWR, 0600);

    ftruncate(player.fd1, sizeof(struct server_to_player_t));
    ftruncate(player.fd2, sizeof(struct player_to_server_t));

    player.stp = (struct server_to_player_t*)mmap(NULL, sizeof(struct server_to_player_t), PROT_WRITE, MAP_SHARED, player.fd1, 0);
    printf("%s",strerror(errno));
    // const char *explain_mmap(void *data, size_t data_size, int prot, int flags, int fildes, off_t offset);
    
    player.pts = (struct player_to_server_t*)mmap(NULL, sizeof(struct player_to_server_t), PROT_READ , MAP_SHARED, player.fd2, 0);
        
    

    /////////////
    //// DANE ///
    /////////////

    time_t tt;
    srand(time(&tt));
    do
    {
        player.spawn_x = rand() % MAP_WIDTH;
        player.spawn_y = rand() % MAP_HEIGHT;
    }
    while(game->map[player.spawn_y][player.spawn_x] != ' ');
    
    player.pos_x = player.spawn_x;
    player.pos_y = player.spawn_y;
    player.deaths = 0;
    player.bush = 0;
    player.money_carried = 0;
    player.money_camped = 0;

    player.stp->pos_x         = player.pos_x;
    player.stp->pos_y         = player.pos_y;
    player.stp->deaths        = player.deaths;
    player.stp->money_carried = player.money_carried;
    player.stp->money_camped  = player.money_camped;
    player.stp->round_number  = game->round_number;
    sem_init(&player.stp->sem,1,0);
    
    /////////////
    //// MAPA ///
    /////////////
    for(int i = -2; i < 3; i++)
    {
        for(int j = -2; j < 3; j++)
        {
            if(player.pos_y + i > 0 && player.pos_y + i < MAP_HEIGHT && player.pos_x + j > 0 && player.pos_x + j < MAP_WIDTH)
            {
                player.stp->map[i+2][j+2] = game->map[i + 2][j + 2];
            }
            else 
            {
                player.stp->map[i+2][j+2] = ' ';
            }
        }
    }
        
    game->players.push_back(player);
}
int player_kill(struct game_t* game, struct player_t* player)
{
    game->player_avatar[player->id - 1] = 0;
    // sem_close(&player->stp->sem);
    sem_destroy(&player->stp->sem);
    munmap(player->stp, sizeof(struct server_to_player_t));
    munmap(player->pts, sizeof(struct player_to_server_t));
    shm_unlink(player->shm_name_stp);
    shm_unlink(player->shm_name_pts);
    close(player->fd1);
    close(player->fd2);
    printf("\n");
    return 0;
}

void players_download_data(struct game_t* game)
{
    for(int i = 0; i < (int)game->players.size(); i++)
    {
        //struct player_t* player = &game->players[i];
        auto player = &game->players[i];
        if(player->pts->quit || player->pts->round_number != game->round_number-1) //Disconnect
        {
            player_kill(game, player);
            game->players.erase(game->players.begin() + i);
            continue;
        }
        switch(player->pts->move)
        {
            case NORTH:
                if(game->map[player->pos_y-1][player->pos_x]=='X') break;
                else if(game->map[player->pos_y][player->pos_x]=='#')
                {
                    if(!player->bush)
                    {
                        player->bush++;
                        break;
                    }
                    else
                    {
                        player->bush=0;
                        player->pos_y--;
                        break;
                    }
                }
                else
                {
                    player->pos_y--;
                    break;
                }

            case SOUTH:
                if(game->map[player->pos_y+1][player->pos_x]=='X') break;
                else if(game->map[player->pos_y][player->pos_x]=='#')
                {
                    if(!player->bush)
                    {
                        player->bush++;
                        break;
                    }
                    else
                    {
                        player->bush=0;
                        player->pos_y++;
                        break;
                    }
                }
                else
                {
                    player->pos_y++;
                    break;
                }
            case WEST:
                if(game->map[player->pos_y][player->pos_x-1]=='X') break;
                else if(game->map[player->pos_y][player->pos_x]=='#')
                {
                    if(!player->bush)
                    {
                        player->bush++;
                        break;
                    }
                    else
                    {
                        player->bush=0;
                        player->pos_x--;
                        break;
                    }
                }
                else
                {
                    player->pos_x--;
                    break;
                }
            case EAST:
                if(game->map[player->pos_y][player->pos_x+1]=='X') break;
                else if(game->map[player->pos_y][player->pos_x]=='#')
                {
                    if(!player->bush)
                    {
                        player->bush++;
                        break;
                    }
                    else
                    {
                        player->bush=0;
                        player->pos_x++;
                        break;
                    }
                }
                else
                {
                    player->pos_x++;
                    break;
                }
            case STOP:
                break;
            default:
                break;
        }

        // Jeżeli pozycja moja == pozycja piniondza:
        //      daj piniondz
        //      zresp piniondz

        for(int j = 0; j < (int)game->gold.size(); j++)
        // for(auto coin = game->gold.begin(); coin != game->gold.end(); coin++)
        {
            auto coin = &game->gold[j];
            if(player->pos_x == coin->x && player->pos_y == coin->y)
            {
                player->money_carried += coin->value;
                if(coin->display == 'D')
                {
                    game->gold.erase(game->gold.begin() + j);
                }
                else
                {
                    do
                    {
                        coin->x = rand() % MAP_WIDTH;
                        coin->y = rand() % MAP_HEIGHT;
                    }
                    while(game->map[coin->y][coin->x] != ' ');
                }
                break;
            }
        }

        // Jeżeli pozycja moja == pozycja obozu:
        //      zdeponuj piniondz
        if(player->pos_x == game->camp.x && player->pos_y == game->camp.y)
        {
            player->money_camped += player->money_carried;
            player->money_carried = 0;
        }
        // Jeżeli pozycja moja == pozycja innego playera:
        //      zabij obu
        //      dropnij ich piniondze
        for(int j = 0; j < (int)game->players.size(); j++)
        {
            if(i == j) continue;
            auto player2 = &game->players[j];
            if(player2->pos_x == player->pos_x && player2->pos_y == player->pos_y)
            {
                if((player2->pos_x == player2->spawn_x && player2->pos_y == player2->spawn_y)
                ||
                (player->pos_x == player->spawn_x && player->pos_y == player->spawn_y))
                {
                    //ignore
                }
                else
                {
                    if(player->money_carried + player2->money_carried) // Dropnij piniondz jeżeli jest niezerowy
                    {
                        gold_add(game, player->money_carried + player2->money_carried, 'D', player->pos_x, player->pos_y); 
                        player->money_carried = 0;
                        player2->money_carried = 0;
                    } 
                    player->pos_x = player->spawn_x;
                    player->pos_y = player->spawn_y;
                    player2->pos_x = player2->spawn_x;
                    player2->pos_y = player2->spawn_y;
                    player->deaths++;
                    player2->deaths++;
                    
                    break;
                }
            }
        }

        // Jeżeli pozycja moja == pozycja bestii:
        //      zabij gracza
        //      dropnij jego piniondz
        for(int i = 0; i < game->beast_count; i++)
        {
            if(game->beasts[i].pos_x == player->pos_x && game->beasts[i].pos_y == player->pos_y)
            {
                if(player->money_carried)
                {
                    gold_add(game, player->money_carried, 'D', player->pos_x, player->pos_y);
                    player->money_carried = 0;
                }
                player->pos_x = player->spawn_x;
                player->pos_y = player->spawn_y;
                player->deaths++;
                break;
            }
        }
    }
    
    
}
void players_upload_data(struct game_t* game)
{
    for(int i = 0; i < (int)game->players.size(); i++)
    {   
        auto player = &game->players[i];  

        player->stp->pos_x         = player->pos_x;
        player->stp->pos_y         = player->pos_y;
        player->stp->deaths        = player->deaths;
        player->stp->money_carried = player->money_carried;
        player->stp->money_camped  = player->money_camped;
        player->stp->round_number  = game->round_number;
        
        for(int i = -2; i < 3; i++)
        {
            for(int j = -2; j < 3; j++)
            {
                if(player->pos_y + i < 0 || player->pos_y + i >= MAP_HEIGHT || player->pos_x + j < 0 || player->pos_x + j >= MAP_WIDTH)
                {
                    player->stp->map[i+2][j+2] = ' ';
                }
                else
                {
                    player->stp->map[i+2][j+2] = game->map[player->pos_y + i][player->pos_x + j];
                }
            }
        }
        sem_post(&player->stp->sem);
    }
}


/////////////////////////////////////
///////////// BESTIE //////////////// 
/////////////////////////////////////
void beast_add(struct game_t* game)
{
    if(game->beast_count >= BEAST_MAX) return;
    struct beast_t* beast = &game->beasts[game->beast_count];
    time_t tt;
    srand(time(&tt));
    do
    {
        beast->pos_x = rand() % MAP_WIDTH;
        beast->pos_y = rand() % MAP_HEIGHT;
    }
    while(game->map[beast->pos_y][beast->pos_x] != ' ');
    beast->id = game->beast_count;
    beast->bush = 0;
    beast->move = STOP;
    beast->game = game;
    beast->status = 0;
    strcpy(beast->sem_name,"Beast_sem");
    beast->sem_name[9] = beast->id + '0';
    beast->sem_name[10] = '\0';

    game->beasts[beast->id].sem = sem_open(beast->sem_name, O_CREAT, 0600, 0);

    pthread_create(&game->beast_pt[game->beast_count], NULL, beast_thread, &game->beasts[game->beast_count]);

    game->beast_count++;
    
}
void* beast_thread(void* arg)
{
    struct beast_t* beast = (struct beast_t*) arg;
    struct game_t* game = beast->game;
    while(1)
    {
        sem_wait(beast->sem);
        beast->move = STOP;
        beast->status = 0;
        for(int i = -2; i < 3; i++)
        {
            for(int j = -2; j < 3; j++)
            {
                if(beast->pos_y + i >= 0 && beast->pos_y + i < MAP_HEIGHT && beast->pos_x + j >= 0 && beast->pos_x + j < MAP_WIDTH && beast->status != 2)
                {
                    if(game->map[beast->pos_y + i][beast->pos_x + j] >= '1' && game->map[beast->pos_y + i][beast->pos_x + j] <= '4')
                    {
                        // Jeżeli gdzieś jest gracz - zastanów sie dobrze
                        // 1. Czy jest w polu widzenia
                        // 2. Czy mam jakiś dostęp do niego
                        if(!check_sight(beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, game))
                        // Kontakt wzrokowy jest, teraz pytanie czy jest droga
                        {
                            beast->move = path_req(game, STOP, beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, 0);
                            if(beast->move)
                            {
                                beast->status = 1;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if(beast->status != 1)
        {
            beast->move = (MOVEREQ)(rand() % 4 + 1);
            beast->status = 0;
        }
    }
}
void* beast_thread_v2(void* arg)
{
    struct beast_t* beast = (struct beast_t*) arg;
    struct game_t* game = beast->game;
    while(1)
    {
        sem_wait(beast->sem);
        beast->move = STOP;
        beast->status = 0;
        for(int i = -2; i < 3; i++)
        {
            for(int j = -2; j < 3; j++)
            {
                if(beast->pos_y + i >= 0 && beast->pos_y + i < MAP_HEIGHT && beast->pos_x + j >= 0 && beast->pos_x + j < MAP_WIDTH && beast->status != 2)
                {
                    if(game->map[beast->pos_y + i][beast->pos_x + j] >= '1' && game->map[beast->pos_y + i][beast->pos_x + j] <= '4')
                    {
                        // Jeżeli gdzieś jest gracz - zastanów sie dobrze
                        // 1. Czy jest w polu widzenia
                        // 2. Czy mam jakiś dostęp do niego
                        if(!check_sight(beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, game))
                        // Kontakt wzrokowy jest, teraz pytanie czy jest droga
                        {
                            beast->move = path_req(game, STOP, beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, 0);
                            if(beast->move)
                            {
                                beast->status = 1;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if(beast->status != 1)
        {
            beast->move = (MOVEREQ)(rand() % 4 + 1);
            beast->status = 0;
        }
    }
}
void* beast_thread_v3(void* arg)
{
    struct beast_t* beast = (struct beast_t*) arg;
    struct game_t* game = beast->game;
    while(1)
    {
        sem_wait(beast->sem);
        beast->move = STOP;
        beast->status = 0;
        for(int i = -2; i < 3; i++)
        {
            for(int j = -2; j < 3; j++)
            {
                if(beast->pos_y + i >= 0 && beast->pos_y + i < MAP_HEIGHT && beast->pos_x + j >= 0 && beast->pos_x + j < MAP_WIDTH && beast->status != 2)
                {
                    if(game->map[beast->pos_y + i][beast->pos_x + j] >= '1' && game->map[beast->pos_y + i][beast->pos_x + j] <= '4')
                    {
                        // Jeżeli gdzieś jest gracz - zastanów sie dobrze
                        // 1. Czy jest w polu widzenia
                        // 2. Czy mam jakiś dostęp do niego
                        if(!check_sight(beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, game))
                        // Kontakt wzrokowy jest, teraz pytanie czy jest droga
                        {
                            beast->move = path_req(game, STOP, beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, 0);
                            if(beast->move)
                            {
                                beast->status = 1;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if(beast->status != 1)
        {
            beast->move = (MOVEREQ)(rand() % 4 + 1);
            beast->status = 0;
        }
    }
}
void* beast_thread_v4(void* arg)
{
    struct beast_t* beast = (struct beast_t*) arg;
    struct game_t* game = beast->game;
    while(1)
    {
        sem_wait(beast->sem);
        beast->move = STOP;
        beast->status = 0;
        for(int i = -2; i < 3; i++)
        {
            for(int j = -2; j < 3; j++)
            {
                if(beast->pos_y + i >= 0 && beast->pos_y + i < MAP_HEIGHT && beast->pos_x + j >= 0 && beast->pos_x + j < MAP_WIDTH && beast->status != 2)
                {
                    if(game->map[beast->pos_y + i][beast->pos_x + j] >= '1' && game->map[beast->pos_y + i][beast->pos_x + j] <= '4')
                    {
                        // Jeżeli gdzieś jest gracz - zastanów sie dobrze
                        // 1. Czy jest w polu widzenia
                        // 2. Czy mam jakiś dostęp do niego
                        if(!check_sight(beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, game))
                        // Kontakt wzrokowy jest, teraz pytanie czy jest droga
                        {
                            beast->move = path_req(game, STOP, beast->pos_x, beast->pos_y, beast->pos_x + j, beast->pos_y + i, 0);
                            if(beast->move)
                            {
                                beast->status = 1;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if(beast->status != 1)
        {
            beast->move = (MOVEREQ)(rand() % 4 + 1);
            beast->status = 0;
        }
    }
}
int check_sight(const int x1, const int y1, const int x2, const int y2, struct game_t* game)
{
    // zmienne pomocnicze
    int d, dx, dy, ai, bi, xi, yi;
    int x = x1, y = y1;
    // ustalenie kierunku rysowania
    if (x1 < x2)
    {
        xi = 1;
        dx = x2 - x1;
    }
    else
    {
        xi = -1;
        dx = x1 - x2;
    }
    // ustalenie kierunku rysowania
    if (y1 < y2)
    {
        yi = 1;
        dy = y2 - y1;
    }
    else
    {
        yi = -1;
        dy = y1 - y2;
    }
    // pierwszy piksel
    if(game->map[y][x] == 'X') return 1;

    // oś wiodąca OX
    if (dx > dy)
    {
        ai = (dy - dx) * 2;
        bi = dy * 2;
        d = bi - dx;
        // pętla po kolejnych x
        while (x != x2)
        {
            // test współczynnika
            if (d >= 0)
            {
                x += xi;
                y += yi;
                d += ai;
            }
            else
            {
                d += bi;
                x += xi;
            }
            if(game->map[y][x] == 'X') return 1;
        }
    }
    // oś wiodąca OY
    else
    {
        ai = ( dx - dy ) * 2;
        bi = dx * 2;
        d = bi - dy;
        // pętla po kolejnych y
        while (y != y2)
        {
            // test współczynnika
            if (d >= 0)
            {
                x += xi;
                y += yi;
                d += ai;
            }
            else
            {
                d += bi;
                y += yi;
            }
            if(game->map[y][x] == 'X') return 1;
        }
    }
    return 0;
}
MOVEREQ path_req(struct game_t* game, MOVEREQ prev, int my_x, int my_y, int search_x, int search_y, int step)
{
    // prev - gdzie ruszyła się poprzednia iteracja, żeby nie chodzić po dwóch tych samych kratkach wkoło
    // step - ile kroków do tej pory zrobiliśmy, żeby nie iść za daleko. Jeżeli bestia widzi gracza, to ma dostęp najdalej w czwartym kroku
    // x, y - gdzie jestem, szukam po prostu cyfry na mapie

    // example - NORTH -> WEST -> NORTH -> WEST
    if(my_x == search_x && my_y == search_y)
    {
        return prev;
    }
    if(step >= 4)
    {
        return STOP;
    }
    if(game->map[my_y][my_x] == 'X')
    {
        return STOP; // STOP == 0 == NULL 
    }
    if(prev != NORTH)
    {
        if(path_req(game, SOUTH, my_x, my_y + 1, search_x, search_y, step + 1)) return SOUTH;
    }
    if(prev != SOUTH)
    {
        if(path_req(game, NORTH, my_x, my_y - 1, search_x, search_y, step + 1)) return NORTH;
    }
    if(prev != WEST)
    {
        if(path_req(game, EAST, my_x + 1, my_y, search_x, search_y, step + 1)) return EAST;
    }
    if(prev != EAST)
    {
        if(path_req(game, WEST, my_x - 1, my_y, search_x, search_y, step + 1)) return WEST;
    }
    return STOP;
}
void beasts_download_data(struct game_t* game)
{
    for(int i = 0; i < game->beast_count; i++)
    {
        struct beast_t* beast = &game->beasts[i];
        switch(beast->move)
        {
            case NORTH:
                if(game->map[beast->pos_y-1][beast->pos_x]=='X') break;
                else if(game->map[beast->pos_y][beast->pos_x]=='#')
                {
                    if(!beast->bush)
                    {
                        beast->bush++;
                        break;
                    }
                    else
                    {
                        beast->bush=0;
                        beast->pos_y--;
                        break;
                    }
                }
                else
                {
                    beast->pos_y--;
                    break;
                }

            case SOUTH:
                if(game->map[beast->pos_y+1][beast->pos_x]=='X') break;
                else if(game->map[beast->pos_y][beast->pos_x]=='#')
                {
                    if(!beast->bush)
                    {
                        beast->bush++;
                        break;
                    }
                    else
                    {
                        beast->bush=0;
                        beast->pos_y++;
                        break;
                    }
                }
                else
                {
                    beast->pos_y++;
                    break;
                }
            case WEST:
                if(game->map[beast->pos_y][beast->pos_x-1]=='X') break;
                else if(game->map[beast->pos_y][beast->pos_x]=='#')
                {
                    if(!beast->bush)
                    {
                        beast->bush++;
                        break;
                    }
                    else
                    {
                        beast->bush=0;
                        beast->pos_x--;
                        break;
                    }
                }
                else
                {
                    beast->pos_x--;
                    break;
                }
            case EAST:
                if(game->map[beast->pos_y][beast->pos_x+1]=='X') break;
                else if(game->map[beast->pos_y][beast->pos_x]=='#')
                {
                    if(!beast->bush)
                    {
                        beast->bush++;
                        break;
                    }
                    else
                    {
                        beast->bush=0;
                        beast->pos_x++;
                        break;
                    }
                }
                else
                {
                    beast->pos_x++;
                    break;
                }
            case STOP:
                break;
            default:
                break;
        }


        // Jeżeli pozycja moja == pozycja innego playera:
        //      zabij playera
        //      dropnij jego piniondze
        for(int i = 0; i < (int)game->players.size(); i++)
        {
            if(game->players[i].pos_x == beast->pos_x && game->players[i].pos_y == beast->pos_y)
            {
                if((game->players[i].pos_x == game->players[i].spawn_x && game->players[i].pos_y == game->players[i].spawn_y))
                {
                    //ignore
                }
                else
                {
                    if(game->players[i].money_carried)
                    {
                        gold_add(game, game->players[i].money_carried, 'D', beast->pos_x, beast->pos_y);
                        game->players[i].money_carried = 0;
                    }
                    game->players[i].pos_x = game->players[i].spawn_x;
                    game->players[i].pos_y = game->players[i].spawn_y; 
                    game->players[i].deaths++;
                    break;
                }
            }
        }
        sem_post(beast->sem);
    }
}


/////////////////////////////////////
////////////// OKNA /////////////////
/////////////////////////////////////
void window_game(struct game_t* game)
{
    map_print(game);
}
void map_print(struct game_t* game)
{
    start_color();
    init_pair(1, COLOR_BLACK, COLOR_YELLOW);
    init_pair(2, COLOR_GREEN, COLOR_YELLOW);
    init_pair(3, COLOR_WHITE, COLOR_GREEN);
    init_pair(4, COLOR_WHITE, COLOR_BLUE);
    init_pair(5, COLOR_RED, COLOR_BLACK);
    for(int i = 0;i<MAP_HEIGHT;i++)
    {
        for (int j = 0;j<MAP_WIDTH;j++)
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
void window_stats(struct game_t* game) 
{
    mvwprintw(game->stats_w, 0, 0, "Server's PID: %lu", game->pid_server);
    mvwprintw(game->stats_w, 1, 0, " Campsite X/Y: %d/%d", game->camp.x, game->camp.y);
    mvwprintw(game->stats_w, 2, 0, " Round number: %d", game->round_number);
    mvwprintw(game->stats_w, 3, 0, "Parameter:\n PID\n Curr X/Y\n Deaths\n\n Coins\n   carried\n   brought");
    for(int i = 0; i < (int)game->players.size(); i++)
    {
        auto player = &game->players[i];
        mvwprintw(game->stats_w,  4, 11 + i * 7, "%lu",    player->pid_player);
        mvwprintw(game->stats_w,  5, 11 + i * 7, "%d/%d ",  player->pos_x, player->pos_y);
        mvwprintw(game->stats_w,  6, 11 + i * 7, "%d",      player->deaths);
        mvwprintw(game->stats_w,  9, 11 + i * 7, "%d",      player->money_carried);
        mvwprintw(game->stats_w, 10, 11 + i * 7, "%d",      player->money_camped);
    }
    mvwprintw(game->stats_w,  13, 0, "Beasts");
    mvwprintw(game->stats_w,  14, 0, "Id");
    mvwprintw(game->stats_w,  15, 0, "Pos");
    mvwprintw(game->stats_w,  16, 0, "Status");
    mvwprintw(game->stats_w,  17, 0, "MOVEREQ");
    
    
    for(int i = 0; i < game->beast_count; i++)
    {
        auto beast = &game->beasts[i];
        mvwprintw(game->stats_w, 14, 11 + i * 7, "%d", beast->id);
        mvwprintw(game->stats_w, 15, 11 + i * 7, "%d/%d", beast->pos_x, beast->pos_y);
        mvwprintw(game->stats_w, 16, 11 + i * 7, "%d", beast->status);
        mvwprintw(game->stats_w, 17, 11 + i * 7, "%d", beast->move);
    }
}


/////////////////////////////////////
///////// MAPA I OBIEKTY ////////////
/////////////////////////////////////
void map_create(struct game_t* game)
{
    game->map = (char**)malloc(MAP_HEIGHT * sizeof(char *));
    for (int i = 0; i < MAP_HEIGHT; i++)
    {
        *(game->map + i) = (char*)malloc(MAP_WIDTH * sizeof(char));
    }
    game->map_default = (char**)malloc(MAP_HEIGHT * sizeof(char *));

    for (int i = 0; i < MAP_HEIGHT; i++)
    {
        *(game->map_default + i) = (char*)malloc(MAP_WIDTH * sizeof(char));
    }
    FILE *fp = fopen(MAP_FILENAME, "rt");

    for (int i = 0; i < MAP_HEIGHT; i++)
    {
        for (int j = 0; j < MAP_WIDTH; j++)
        {   
            game->map_default[i][j] = (char)getc(fp);
            game->map[i][j] = game->map_default[i][j];
        }
        getc(fp); //Wyłapywanie '\n'
    }
    fclose(fp);
}
void map_clear(struct game_t* game)
{
    for(int i = 0;i < MAP_HEIGHT;i++)
    {
        for (int j = 0;j < MAP_WIDTH;j++)
        {
            game->map[i][j] = game->map_default[i][j];
        }
    }
}
int gold_add_rand(struct game_t* game, int value, char display) //Weryfikuje miejsce, potem wywołuje tą niżej
{
    int x;
    int y;
    time_t tt;
    srand(time(&tt));
    do
    {
        x = rand() % MAP_WIDTH;
        y = rand() % MAP_HEIGHT;
    }
    while(game->map[y][x] != ' ');
    return gold_add(game, value, display, x, y);

}
int gold_add(struct game_t* game, int value, char display, int x, int y)
{
    game->gold.push_back({x, y, value, display});
    game->map[y][x] = display;
    return 0;
}
void gold_update(struct game_t* game)
{
    for(int i = 0; i < (int)game->gold.size(); i++)
    {
        auto coin = &game->gold[i];
        game->map[coin->y][coin->x] = coin->display;
    }
}
void camp_generate(struct game_t* game)
{
    int x;
    int y;
    time_t tt;
    srand(time(&tt));
    do
    {
        x = rand() % MAP_WIDTH;
        y = rand() % MAP_HEIGHT;
    }
    while(game->map[y][x] != ' ');
    game->camp.x = x;
    game->camp.y = y;
    game->camp.display = 'A';
    game->camp.value = 0;
}
void camp_update(struct game_t* game)
{
    game->map[game->camp.y][game->camp.x] = game->camp.display;
}
void players_update(struct game_t* game)
{
    for(int i = 0; i < (int)game->players.size(); i++)
    {
        auto player = &game->players[i];
        game->map[player->pos_y][player->pos_x] = (char)player->id + '0';
    }
}
void beasts_update(struct game_t* game)
{
    for(int i = 0; i < game->beast_count; i++)
    {
        auto beast = &game->beasts[i];
        game->map[beast->pos_y][beast->pos_x] = '*';
    }
}