#ifndef MALOSPAL_GAME_INPUT_H
#define MALOSPAL_GAME_INPUT_H

#include <stdbool.h>
#include <stdint.h>

bool game_read_key(uint8_t *value, bool *lost);
void game_discard_input(void);
void game_wait_tick(void);

#endif
