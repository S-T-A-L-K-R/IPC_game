#include <stdio.h>
#include <curses.h>
#include <unistd.h>
#include <pthread.h>

#include "player_game.h"

int main(int argc, char **argv)
{
    struct game_t *game = (struct game_t*)malloc(sizeof(struct game_t));
    game_init(game);
    if(game->connected)
    {
        pthread_create(&game->gpmt_pt, NULL, game_play_main_thread, game);
        pthread_create(&game->gpgt_pt, NULL, game_play_getch_thread, game);
        pthread_create(&game->scct_pt, NULL, server_connection_check_thread, game);
        sem_wait(&game->quit_sem);
        pthread_cancel(game->gpmt_pt);
        pthread_cancel(game->gpgt_pt);
    }
    else
    {
        pthread_cancel(game->gpmt_pt);
        pthread_cancel(game->gpgt_pt);
        pthread_cancel(game->scct_pt);
        
    }
    printf("\nBye!");
    getchar();
    endwin();
    sem_destroy(&game->quit_sem);
    free(game);
    return 0;
}