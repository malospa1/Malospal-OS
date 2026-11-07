#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/game.h>
#include <malospal/keyboard.h>
#include <malospal/input.h>
#include <malospal/screen.h>
#include <malospal/serial.h>
#include <malospal/timer.h>
#include "snake_logic.h"
#include "game_input.h"

enum play_state { READY, PLAYING, PAUSED, LOST, WON };
enum { BACKGROUND = 0x101418, INK = 0xdce6ea, GREEN = 0x4fd6a2,
       HEAD = 0xc4f477, FOOD = 0xffb65b, RED = 0xff7085 };
static unsigned int best;

struct game_view {
    unsigned int x, y, cell;
    uint8_t previous[SNAKE_CELLS];
};

static void number(char *target, unsigned int value) {
    for (unsigned int i = 4; i > 0; --i) { target[i - 1] = (char)('0' + value % 10); value /= 10; }
}

static const char *state_name(enum play_state state) {
    switch (state) {
        case READY: return "READY";
        case PLAYING: return "PLAYING";
        case PAUSED: return "PAUSED";
        case LOST: return "GAME OVER";
        case WON: return "YOU WIN";
    }
    return "";
}

static void log_state(enum play_state state, const struct snake *game) {
    const char *part = "SNAKE ";
    while (*part) { serial_write_byte(*part++); }
    part = state_name(state);
    while (*part) { serial_write_byte(*part++); }
    char score[] = " score=0000\r\n";
    number(score + 7, game->length - 3);
    for (unsigned int i = 0; score[i]; ++i) { serial_write_byte(score[i]); }
}

static void draw_tile(const struct snake *game, struct game_view *view, unsigned int x, unsigned int y, bool force) {
    unsigned int cell = snake_cell_at(game, x, y);
    if (x == game->head_x && y == game->head_y) { cell = 6 + cell; }
    else if (cell && cell != SNAKE_FOOD) { cell = 1; }
    const unsigned int index = y * SNAKE_WIDTH + x;
    if (!force && view->previous[index] == cell) { return; }
    view->previous[index] = (uint8_t)cell;
    const unsigned int size = view->cell, px = view->x + x * size, py = view->y + y * size;
    screen_fill_rect(px, py, size, size, ((x + y) & 1) ? 0x1d272d : 0x192127);
    if (!cell) { return; }
    if (cell == SNAKE_FOOD) {
        /* An original 8x8 bitmap fruit sprite, scaled to the board cells. */
        static const uint8_t sprite[8] = {0x10,0x08,0x7e,0xff,0xff,0xff,0x7e,0x3c};
        unsigned int unit = size / 10;
        if (!unit) { unit = 1; }
        const unsigned int offset = (size > 8 * unit) ? (size - 8 * unit) / 2 : 0;
        for (unsigned int iy = 0; iy < 8 && iy * unit < size; ++iy) {
            for (unsigned int ix = 0; ix < 8 && ix * unit < size; ++ix) {
                if (sprite[iy] & (1u << ix)) {
                    screen_fill_rect(px + offset + ix * unit, py + offset + iy * unit,
                                     unit, unit, iy < 2 ? GREEN : FOOD);
                }
            }
        }
    } else {
        const unsigned int inset = size >= 12 ? 2 : 1;
        screen_fill_rect(px + inset, py + inset, size - 2 * inset, size - 2 * inset, cell > 6 ? HEAD : GREEN);
        if (cell > 6 && size >= 12) {
            unsigned int a = size / 3, b = size * 2 / 3, forward = size * 3 / 4;
            const unsigned int direction = cell - 7, eye = size / 8;
            if (direction == SNAKE_LEFT || direction == SNAKE_UP) { forward = size / 4; }
            if (direction == SNAKE_UP || direction == SNAKE_DOWN) {
                screen_fill_rect(px + a, py + forward, eye, eye, BACKGROUND);
                screen_fill_rect(px + b, py + forward, eye, eye, BACKGROUND);
            } else {
                screen_fill_rect(px + forward, py + a, eye, eye, BACKGROUND);
                screen_fill_rect(px + forward, py + b, eye, eye, BACKGROUND);
            }
        }
    }
}

static void render(const struct snake *game, struct game_view *view, enum play_state state, bool full) {
    const struct screen_size screen = screen_size();
    const unsigned int scale = screen.width >= 640 ? 3 : 2;
    if (full) {
        screen_fill_rect(0, 0, screen.width, screen.height, BACKGROUND);
        screen_draw_text(24, 26, "SNAKE", scale, INK);
        screen_fill_rect(view->x - 2, view->y - 2, SNAKE_WIDTH * view->cell + 4,
                         SNAKE_HEIGHT * view->cell + 4, 0x3d5059);
    }
    char score[] = "SCORE 0000  BEST 0000";
    number(score + 6, game->length - 3);
    number(score + 17, best);
    screen_fill_rect(screen.width - 192, 28, 168, 16, BACKGROUND);
    screen_draw_text(screen.width - 192, 28, score, 1, INK);
    screen_fill_rect(24, 66, screen.width - 48, 20, BACKGROUND);
    screen_draw_text(24, 66, state_name(state), 2, state == LOST ? RED : state == WON ? GREEN : FOOD);
    for (unsigned int y = 0; y < SNAKE_HEIGHT; ++y) {
        for (unsigned int x = 0; x < SNAKE_WIDTH; ++x) { draw_tile(game, view, x, y, full); }
    }
}

void snake_run(void) {
    const struct screen_size screen = screen_size();
    if (screen.width < 320 || screen.height < 240) {
        debug_write("[error] Snake needs a framebuffer of at least 320x240\n");
        return;
    }
    if (!arch_irq_enabled()) { debug_write("[error] Snake needs timer interrupts\n"); return; }
    if (!screen_graphics_begin()) { debug_write("[error] Screen is busy\n"); return; }
    struct snake game;
    struct game_view view = {0};
    view.cell = (screen.width - 48) / SNAKE_WIDTH;
    if (view.cell > (screen.height - 128) / SNAKE_HEIGHT) { view.cell = (screen.height - 128) / SNAKE_HEIGHT; }
    if (view.cell > 32) { view.cell = 32; }
    view.x = (screen.width - view.cell * SNAKE_WIDTH) / 2;
    view.y = 96 + (screen.height - 128 - view.cell * SNAKE_HEIGHT) / 2;
    enum play_state state = READY;
    snake_initialize(&game, (uint32_t)timer_ticks());
    render(&game, &view, state, true);
    log_state(state, &game);
    uint64_t last = timer_ticks();
    bool quit = false;
    while (!quit) {
        bool dirty = false;
        for (unsigned int i = 0; i < 64; ++i) {
            uint8_t key;
            bool lost;
            if (!game_read_key(&key, &lost)) { break; }
            if (lost) {
                if (state == PLAYING) { state = PAUSED; dirty = true; }
                continue;
            }
            if (key >= 'A' && key <= 'Z') { key += 'a' - 'A'; }
            if (key == 'q' || key == 0x1b) { quit = true; break; }
            if (key == 'r') {
                snake_initialize(&game, (uint32_t)timer_ticks());
                state = READY;
                dirty = true;
            } else if (key == ' ' || key == 'p' || key == '\r') {
                if (state == READY || state == PAUSED) { state = PLAYING; }
                else if (state == PLAYING) { state = PAUSED; }
                dirty = true;
            } else {
                int direction = key == 'w' || key == KEY_UP ? SNAKE_UP :
                                key == 's' || key == KEY_DOWN ? SNAKE_DOWN :
                                key == 'a' || key == KEY_LEFT ? SNAKE_LEFT :
                                key == 'd' || key == KEY_RIGHT ? SNAKE_RIGHT : -1;
                if (direction >= 0 && state != LOST && state != WON) {
                    snake_redirect(&game, (enum snake_direction)direction);
                    if (state == READY) { state = PLAYING; dirty = true; }
                }
            }
            if (dirty) { last = timer_ticks(); }
        }
        if (!input_ready()) { quit = true; }
        if (quit) { break; }
        const uint64_t now = timer_ticks();
        const unsigned int period = game.length - 3 >= 30 ? 8 : 18 - (game.length - 3) / 3;
        if (state == PLAYING && now - last >= period) {
            const enum snake_result result = snake_step(&game);
            if (result == SNAKE_LOST) { state = LOST; }
            if (result == SNAKE_WON) { state = WON; }
            if (game.length - 3 > best) { best = game.length - 3; }
            last = now;
            render(&game, &view, state, false);
            if (result != SNAKE_MOVED) { log_state(state, &game); }
        } else if (dirty) {
            render(&game, &view, state, false);
            log_state(state, &game);
        }
        /* PIT wakes us each tick; all game work runs outside the IRQ handler. */
        game_wait_tick();
    }
    game_discard_input();
    screen_graphics_end();
    debug_write("Snake closed. Score="); debug_u64(game.length - 3);
    debug_write(" Best="); debug_u64(best); debug_write("\n");
}
