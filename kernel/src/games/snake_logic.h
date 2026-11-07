#ifndef MALOSPAL_SNAKE_LOGIC_H
#define MALOSPAL_SNAKE_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

enum { SNAKE_WIDTH = 24, SNAKE_HEIGHT = 18, SNAKE_CELLS = SNAKE_WIDTH * SNAKE_HEIGHT };
enum snake_direction { SNAKE_RIGHT, SNAKE_UP, SNAKE_LEFT, SNAKE_DOWN };
enum snake_cell { SNAKE_EMPTY, SNAKE_BODY_RIGHT, SNAKE_BODY_UP,
                  SNAKE_BODY_LEFT, SNAKE_BODY_DOWN, SNAKE_FOOD };
enum snake_result { SNAKE_MOVED, SNAKE_ATE, SNAKE_LOST, SNAKE_WON };
struct snake {
    uint8_t cells[(SNAKE_CELLS * 3 + 7) / 8 + 1];
    unsigned int head_x, head_y, tail_x, tail_y, length;
    enum snake_direction next;
    enum snake_result result;
    uint32_t random;
};

void snake_initialize(struct snake *game, uint32_t seed);
enum snake_cell snake_cell_at(const struct snake *game, unsigned int x, unsigned int y);
void snake_redirect(struct snake *game, enum snake_direction direction);
enum snake_result snake_step(struct snake *game);

#endif
