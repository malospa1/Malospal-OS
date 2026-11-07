#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/games/snake_logic.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Snake failure %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void fixture_cell(struct snake *game, unsigned int x, unsigned int y, unsigned int value) {
    for (unsigned int bit = 0; bit < 3; ++bit) {
        unsigned int pos = (y * SNAKE_WIDTH + x) * 3 + bit;
        game->cells[pos / 8] = (uint8_t)((game->cells[pos / 8] & ~(1u << (pos % 8))) |
            (((value >> bit) & 1u) << (pos % 8)));
    }
}

static void invariant(const struct snake *game) {
    unsigned int body = 0, food = 0;
    CHECK(game->head_x < SNAKE_WIDTH && game->tail_x < SNAKE_WIDTH);
    CHECK(game->head_y < SNAKE_HEIGHT && game->tail_y < SNAKE_HEIGHT);
    for (unsigned int y = 0; y < SNAKE_HEIGHT; ++y) {
        for (unsigned int x = 0; x < SNAKE_WIDTH; ++x) {
            const enum snake_cell cell = snake_cell_at(game, x, y);
            CHECK(cell <= SNAKE_FOOD);
            body += cell > SNAKE_EMPTY && cell < SNAKE_FOOD;
            food += cell == SNAKE_FOOD;
        }
    }
    CHECK(body == game->length && game->length >= 3 && game->length <= SNAKE_CELLS);
    CHECK(food == (game->result == SNAKE_WON ? 0u : 1u));
    CHECK(game->cells[sizeof(game->cells) - 1] == 0);
}

int main(void) {
    struct snake game;
    snake_initialize(&game, 0);
    invariant(&game);
    snake_redirect(&game, SNAKE_LEFT); CHECK(game.next == SNAKE_RIGHT);
    snake_redirect(&game, SNAKE_UP);
    snake_redirect(&game, SNAKE_LEFT); CHECK(game.next == SNAKE_UP);
    snake_redirect(&game, (enum snake_direction)99); CHECK(game.next == SNAKE_UP);
    snake_initialize(&game, 1234);
    for (unsigned int i = 0; i < 4; ++i) { CHECK(snake_step(&game) == SNAKE_MOVED); invariant(&game); }
    CHECK(snake_step(&game) == SNAKE_ATE && game.length == 4); invariant(&game);
    for (unsigned int i = 0; i < SNAKE_WIDTH; ++i) { CHECK(snake_step(&game) != SNAKE_LOST); invariant(&game); }
    CHECK(game.head_x == 17 && game.head_y == 9);
    snake_redirect(&game, SNAKE_UP);
    for (unsigned int i = 0; i < SNAKE_HEIGHT; ++i) { CHECK(snake_step(&game) != SNAKE_LOST); invariant(&game); }
    CHECK(game.head_y == 9);

    game = (struct snake){.head_x = 1, .head_y = 0, .tail_x = 0, .tail_y = 0,
                          .length = 4, .next = SNAKE_LEFT, .random = 1};
    fixture_cell(&game, 0, 0, SNAKE_BODY_DOWN);
    fixture_cell(&game, 0, 1, SNAKE_BODY_RIGHT);
    fixture_cell(&game, 1, 1, SNAKE_BODY_UP);
    fixture_cell(&game, 1, 0, SNAKE_BODY_UP);
    fixture_cell(&game, 23, 17, SNAKE_FOOD);
    struct snake collision = game;
    collision.next = SNAKE_DOWN;
    CHECK(snake_step(&collision) == SNAKE_LOST);
    CHECK(!memcmp(collision.cells, game.cells, sizeof(game.cells)));
    CHECK(snake_step(&collision) == SNAKE_LOST); invariant(&collision);
    CHECK(snake_step(&game) == SNAKE_MOVED && game.head_x == 0 && game.head_y == 0);
    invariant(&game);

    /* A serpentine path leaves two cells: bounded placement must choose the
     * last empty cell, then the next step wins without trying to place food. */
    game = (struct snake){.head_x = 2, .head_y = 17, .length = SNAKE_CELLS - 2,
                          .next = SNAKE_LEFT, .random = 42};
    for (unsigned int y = 0; y < SNAKE_HEIGHT; ++y) {
        for (unsigned int x = 0; x < SNAKE_WIDTH; ++x) {
            if (y == 17 && x < 2) { continue; }
            unsigned int direction = (y & 1) ? SNAKE_BODY_LEFT : SNAKE_BODY_RIGHT;
            if ((y & 1) ? x == 0 : x == 23) { direction = SNAKE_BODY_DOWN; }
            fixture_cell(&game, x, y, direction);
        }
    }
    fixture_cell(&game, 1, 17, SNAKE_FOOD);
    invariant(&game);
    CHECK(snake_step(&game) == SNAKE_ATE && game.length == SNAKE_CELLS - 1);
    CHECK(snake_cell_at(&game, 0, 17) == SNAKE_FOOD); invariant(&game);
    CHECK(snake_step(&game) == SNAKE_WON); invariant(&game);
    CHECK(snake_step(&game) == SNAKE_WON);

    uint32_t random = 71;
    snake_initialize(&game, 1);
    for (unsigned int i = 0; i < 100000; ++i) {
        random = random * 1664525u + 1013904223u;
        snake_redirect(&game, (enum snake_direction)(random >> 30));
        const enum snake_result result = snake_step(&game);
        invariant(&game);
        if (result == SNAKE_LOST || result == SNAKE_WON) { snake_initialize(&game, random); }
    }
    puts("PASS: Snake packed cells, reversal, growth, wrapping, tail entry, collision, full board and 100000 steps.");
    return 0;
}
