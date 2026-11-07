#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/game.h>
#include <malospal/keyboard.h>
#include <malospal/input.h>
#include <malospal/screen.h>
#include <malospal/serial.h>
#include <malospal/timer.h>
#include "game_input.h"
#include "slots_logic.h"
#include "slots_sprites.h"

enum { BG = 0x101418, INK = 0xe8eef0, MUTED = 0x90a5ad, CORAL = 0xf06b78, MINT = 0x5de0b2 };
struct slots_view { unsigned int width, height, left, top, reel_width, reel_height; };
static struct slots_game game;
static bool initialized;

static void digits(char *text, uint32_t value, unsigned int count) {
    while (count) { text[--count] = (char)('0' + value % 10); value /= 10; }
}

static uint32_t faded(uint32_t rgb) {
    return (((((rgb >> 16) & 255) + 3 * 220) / 4) << 16) |
           (((((rgb >> 8) & 255) + 3 * 228) / 4) << 8) | ((rgb & 255) + 3 * 232) / 4;
}

static void icon(unsigned int symbol, int x, int y, unsigned int scale,
                 unsigned int top, unsigned int bottom, bool dim) {
    const struct slots_sprite *sprite = &slots_sprites[symbol % SLOTS_SYMBOLS];
    for (unsigned int row = 0; row < 16; ++row) {
        int py = y + (int)(row * scale), end = py + (int)scale;
        if (end <= (int)top || py >= (int)bottom) { continue; }
        if (py < (int)top) { py = (int)top; }
        if (end > (int)bottom) { end = (int)bottom; }
        for (unsigned int col = 0; col < 16;) {
            const unsigned int mask = 1u << (15 - col);
            const unsigned int type = (sprite->detail[row] & mask) ? 2 : (sprite->shape[row] & mask) ? 1 : 0;
            unsigned int run = col + 1;
            while (run < 16) {
                const unsigned int bit = 1u << (15 - run);
                const unsigned int next = (sprite->detail[row] & bit) ? 2 : (sprite->shape[row] & bit) ? 1 : 0;
                if (next != type) { break; }
                ++run;
            }
            if (type) {
                uint32_t rgb = type == 2 ? sprite->accent : sprite->color;
                screen_fill_rect((unsigned int)x + col * scale, (unsigned int)py, (run - col) * scale,
                                 (unsigned int)(end - py), dim ? faded(rgb) : rgb);
            }
            col = run;
        }
    }
}

static void text_center(unsigned int center, unsigned int y, const char *text, unsigned int scale, uint32_t rgb) {
    unsigned int length = 0;
    while (text[length]) { ++length; }
    screen_draw_text(center - length * 4 * scale, y, text, scale, rgb);
}

static void counter(unsigned int center, const char *label, uint32_t value) {
    char text[] = "000000";
    digits(text, value, 6);
    text_center(center, 84, label, 1, MUTED);
    text_center(center, 102, text, 2, INK);
}

static void draw_reel(const struct slots_view *v, unsigned int reel, uint64_t now) {
    const unsigned int x = v->left + reel * (v->reel_width + 12), y = v->top;
    const bool stopped = slots_reel_stopped(&game, reel, now);
    const bool match = !game.spinning && game.last &&
        (game.reels[reel] == game.reels[(reel + 1) % 3] || game.reels[reel] == game.reels[(reel + 2) % 3]);
    screen_fill_rect(x - 3, y - 3, v->reel_width + 6, v->reel_height + 6, match ? MINT : 0x465660);
    screen_fill_rect(x, y, v->reel_width, v->reel_height, 0xdce4e8);
    unsigned int scale = (v->reel_width - 32) / 16;
    if (scale > 10) { scale = 10; }
    const unsigned int pitch = 16 * scale + 24, middle = y + v->reel_height / 2;
    screen_fill_rect(x, middle - 8 * scale - 8, v->reel_width, 16 * scale + 16, 0xf2f6f8);
    const uint64_t elapsed = now - game.started;
    const unsigned int symbol = stopped ? (game.spinning ? game.target[reel] : game.reels[reel]) :
        (6 - (unsigned int)((elapsed / 12) % 6) + reel * 2) % 6;
    const int offset = stopped ? 0 : (int)((elapsed % 12) * pitch / 12);
    const int left = (int)(x + (v->reel_width - 16 * scale) / 2);
    const int center = (int)middle - (int)(8 * scale) + offset;
    icon((symbol + 5) % 6, left, center - (int)pitch, scale, y, y + v->reel_height, true);
    icon(symbol, left, center, scale, y, y + v->reel_height, false);
    icon((symbol + 1) % 6, left, center + (int)pitch, scale, y, y + v->reel_height, true);
    screen_fill_rect(x, middle - 2, 6, 4, CORAL);
    screen_fill_rect(x + v->reel_width - 6, middle - 2, 6, 4, CORAL);
}

static void render(const struct slots_view *v, uint64_t now, bool full) {
    if (full) {
        screen_fill_rect(0, 0, v->width, v->height, BG);
        screen_draw_text(32, 26, "REEL MACHINE", v->width >= 800 ? 3 : 2, INK);
        screen_draw_text(v->width - 120, 30, "MALOSPAL ARCADE", 1, CORAL);
        screen_fill_rect(32, 64, v->width - 64, 2, 0x33424a);
        screen_draw_text(32, v->height - 100, "3 MATCH", 1, MUTED);
        screen_draw_text(v->width - 128, v->height - 100, "2 MATCH = 5", 1, MUTED);
        const unsigned int column = (v->width - 64) / SLOTS_SYMBOLS;
        for (unsigned int i = 0; i < SLOTS_SYMBOLS; ++i) {
            const unsigned int center = 32 + column * i + column / 2;
            icon(i, (int)center - 16, (int)v->height - 78, 2, 0, v->height, false);
            char points[] = "000";
            digits(points, slots_triple_points(i), 3);
            text_center(center, v->height - 30, points, 1, INK);
        }
    }
    screen_fill_rect(0, 80, v->width, 48, BG);
    counter(v->width / 4, "SCORE", game.score);
    counter(v->width / 2, "SPINS", game.spins);
    counter(v->width * 3 / 4, "LAST", game.last);
    for (unsigned int i = 0; i < SLOTS_REELS; ++i) { draw_reel(v, i, now); }
    const unsigned int lever = v->left + 3 * (v->reel_width + 12) + 14;
    screen_fill_rect(lever - 16, v->top - 20, 40, v->reel_height + 40, BG);
    unsigned int handle = v->top + 24;
    if (game.spinning && now - game.started < 24) { handle += v->reel_height / 2; }
    screen_fill_rect(lever - 10, v->top + v->reel_height - 32, 28, 32, 0x465660);
    screen_fill_rect(lever, handle, 8, v->top + v->reel_height - handle, 0x90a5ad);
    screen_fill_rect(lever - 8, handle - 8, 24, 24, CORAL);
    screen_fill_rect(lever - 4, handle - 4, 8, 4, 0xffb6bb);
    const unsigned int status_y = v->top + v->reel_height + 22;
    screen_fill_rect(0, status_y, v->width, 24, BG);
    char result[] = "MATCH +000";
    digits(result + 7, game.last, 3);
    const char *status = game.spinning ? "SPINNING" : game.spins == 0 ? "READY" : game.last ? result : "NO MATCH";
    text_center(v->width / 2, status_y, status, 2, game.spinning ? CORAL : game.last ? MINT : INK);
}

static void uart_text(const char *text) { while (*text) { serial_write_byte(*text++); } }
static void uart_number(uint32_t value) {
    char text[10];
    unsigned int count = 0;
    do { text[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count) { serial_write_byte(text[--count]); }
}
static void log_state(void) {
    uart_text(game.spinning ? "SLOTS spinning" : "SLOTS idle");
    uart_text(" score="); uart_number(game.score);
    uart_text(" spins="); uart_number(game.spins);
    uart_text(" last="); uart_number(game.last);
    uart_text(" reels=");
    for (unsigned int i = 0; i < SLOTS_REELS; ++i) {
        if (i) { serial_write_byte(','); }
        uart_number(game.reels[i]);
    }
    uart_text("\r\n");
}

void slots_run(void) {
    const struct screen_size screen = screen_size();
    if (screen.width < 640 || screen.height < 480) {
        debug_write("[error] Slots needs a framebuffer of at least 640x480\n"); return;
    }
    if (!arch_irq_enabled()) { debug_write("[error] Slots needs timer interrupts\n"); return; }
    if (!screen_graphics_begin()) { debug_write("[error] Screen is busy\n"); return; }
    if (!initialized) { slots_initialize(&game, timer_ticks()); initialized = true; }
    struct slots_view view = {.width = screen.width, .height = screen.height};
    view.reel_width = (screen.width - 128 - 24) / 3;
    if (view.reel_width > 220) { view.reel_width = 220; }
    view.reel_height = screen.height - 304;
    if (view.reel_height > 300) { view.reel_height = 300; }
    view.left = (screen.width - (3 * view.reel_width + 24 + 64)) / 2;
    view.top = (screen.height - view.reel_height) / 2;
    uint64_t painted = timer_ticks();
    render(&view, painted, true);
    log_state();
    bool quit = false;
    while (!quit) {
        bool changed = false;
        for (unsigned int i = 0; i < 32; ++i) {
            uint8_t key;
            bool lost;
            if (!game_read_key(&key, &lost)) { break; }
            if (lost) { game_discard_input(); break; }
            if (key >= 'A' && key <= 'Z') { key += 'a' - 'A'; }
            if (key == 0x1b || key == 'q') { quit = true; break; }
            if (key == ' ' || key == '\r') { changed |= slots_start(&game, timer_ticks()); }
            if (key == 'r' && !game.spinning) { slots_initialize(&game, timer_ticks()); changed = true; }
        }
        if (!input_ready()) { quit = true; }
        if (quit) { break; }
        const uint64_t now = timer_ticks();
        changed |= slots_update(&game, now);
        if (changed || (game.spinning && now - painted >= 5)) {
            render(&view, now, false);
            painted = now;
            if (changed) { log_state(); }
        }
        game_wait_tick();
    }
    /* An early exit settles the already-chosen result exactly once. */
    (void)slots_finish(&game);
    game_discard_input();
    screen_graphics_end();
    debug_write("Slots closed. Score="); debug_u64(game.score);
    debug_write(" Spins="); debug_u64(game.spins); debug_write("\n");
}
