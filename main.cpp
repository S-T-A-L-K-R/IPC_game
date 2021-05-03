#include <stdio.h>
#include <curses.h>
#include <unistd.h>
#include <pthread.h>

#include "game.h"

int main(int argc, char **argv)
{
    struct game_t *game = (struct game_t*)malloc(sizeof(struct game_t));
    game_init(game);
    pthread_create(&game->plt_pt, NULL, player_listener_thread, game);
    pthread_create(&game->gmt_pt, NULL, game_main_thread, game);

    pthread_join(game->gmt_pt, NULL);

    game_stop(game);

    return 0;
}