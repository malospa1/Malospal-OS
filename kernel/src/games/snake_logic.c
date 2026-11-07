/* Adapted from SDL's public-domain Snake example. See vendor/sdl-snake/README.md. */
#include "snake_logic.h"

enum snake_cell snake_cell_at(const struct snake *game, unsigned int x, unsigned int y) {
    if (x >= SNAKE_WIDTH || y >= SNAKE_HEIGHT) { return SNAKE_EMPTY; }
    const unsigned int shift = (y * SNAKE_WIDTH + x) * 3, pos = shift / 8;
    const unsigned int bits = game->cells[pos] | (unsigned int)game->cells[pos + 1] << 8;
    return (enum snake_cell)((bits >> (shift % 8)) & 7);
}

static void put_cell(struct snake *game, unsigned int x, unsigned int y, enum snake_cell cell) {
    const unsigned int shift = (y * SNAKE_WIDTH + x) * 3, pos = shift / 8;
    unsigned int bits = game->cells[pos] | (unsigned int)game->cells[pos + 1] << 8;
    bits = (bits & ~(7u << (shift % 8))) | ((unsigned int)cell << (shift % 8));
    game->cells[pos] = (uint8_t)bits;
    game->cells[pos + 1] = (uint8_t)(bits >> 8);
}

static void advance(unsigned int *x, unsigned int *y, enum snake_direction direction) {
    switch (direction) {
        case SNAKE_RIGHT: *x = (*x + 1) % SNAKE_WIDTH; break;
        case SNAKE_UP: *y = (*y + SNAKE_HEIGHT - 1) % SNAKE_HEIGHT; break;
        case SNAKE_LEFT: *x = (*x + SNAKE_WIDTH - 1) % SNAKE_WIDTH; break;
        case SNAKE_DOWN: *y = (*y + 1) % SNAKE_HEIGHT; break;
    }
}

static void place_food(struct snake *game) {
    game->random ^= game->random << 13;
    game->random ^= game->random >> 17;
    game->random ^= game->random << 5;
    unsigned int skip = game->random % (SNAKE_CELLS - game->length);
    for (unsigned int y = 0; y < SNAKE_HEIGHT; ++y) {
        for (unsigned int x = 0; x < SNAKE_WIDTH; ++x) {
            if (snake_cell_at(game, x, y) == SNAKE_EMPTY && skip-- == 0) {
                put_cell(game, x, y, SNAKE_FOOD);
                return;
            }
        }
    }
}

void snake_initialize(struct snake *game, uint32_t seed) {
    *game = (struct snake){.head_x = SNAKE_WIDTH / 2, .head_y = SNAKE_HEIGHT / 2,
        .tail_x = SNAKE_WIDTH / 2 - 2, .tail_y = SNAKE_HEIGHT / 2, .length = 3,
        .next = SNAKE_RIGHT, .random = seed ? seed : 1};
    for (unsigned int x = game->tail_x; x <= game->head_x; ++x) {
        put_cell(game, x, game->head_y, SNAKE_BODY_RIGHT);
    }
    put_cell(game, game->head_x + 5, game->head_y, SNAKE_FOOD);
}

void snake_redirect(struct snake *game, enum snake_direction direction) {
    if ((unsigned int)direction > SNAKE_DOWN) { return; }
    const unsigned int current = snake_cell_at(game, game->head_x, game->head_y) - 1;
    if (((unsigned int)direction + 2) % 4 != current) { game->next = direction; }
}

enum snake_result snake_step(struct snake *game) {
    if (game->result == SNAKE_LOST || game->result == SNAKE_WON) { return game->result; }
    unsigned int x = game->head_x, y = game->head_y;
    advance(&x, &y, game->next);
    const enum snake_cell cell = snake_cell_at(game, x, y);
    const bool eating = cell == SNAKE_FOOD;
    if (cell != SNAKE_EMPTY && !eating && !(x == game->tail_x && y == game->tail_y)) {
        return game->result = SNAKE_LOST;
    }
    if (!eating) {
        const enum snake_direction tail_dir = (enum snake_direction)
            (snake_cell_at(game, game->tail_x, game->tail_y) - 1);
        put_cell(game, game->tail_x, game->tail_y, SNAKE_EMPTY);
        advance(&game->tail_x, &game->tail_y, tail_dir);
    }
    const enum snake_cell direction = (enum snake_cell)(game->next + 1);
    put_cell(game, game->head_x, game->head_y, direction);
    game->head_x = x;
    game->head_y = y;
    put_cell(game, x, y, direction);
    if (eating) {
        if (++game->length == SNAKE_CELLS) { return game->result = SNAKE_WON; }
        place_food(game);
    }
    return game->result = eating ? SNAKE_ATE : SNAKE_MOVED;
}
