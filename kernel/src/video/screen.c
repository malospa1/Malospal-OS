#include <malospal/arch.h>
#include <malospal/screen.h>
#include "../../vendor/font8x8/font8x8_basic.h"

enum { MARGIN = 16, CELL_W = 8, CELL_H = 16, MAX_COLS = 240, MAX_ROWS = 128 };
static struct boot_framebuffer framebuffer;
static unsigned int columns, rows, column, row;
static unsigned char cells[MAX_ROWS][MAX_COLS];
static uint32_t foreground, background;
static bool ready, busy, graphics;

static uint32_t color(uint32_t rgb) {
    return ((rgb >> 16) & 255) << framebuffer.red_shift |
           ((rgb >> 8) & 255) << framebuffer.green_shift |
           (rgb & 255) << framebuffer.blue_shift;
}

static void pixel(unsigned int x, unsigned int y, uint32_t value) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)(framebuffer.address +
        y * framebuffer.pitch + x * (framebuffer.bpp / 8));
    for (unsigned int i = 0; i < framebuffer.bpp / 8; ++i) {
        p[i] = (uint8_t)(value >> (i * 8));
    }
}

static void draw(unsigned int x, unsigned int y) {
    const unsigned char ch = cells[y][x];
    for (unsigned int py = 0; py < CELL_H; ++py) {
        const uint8_t bits = (uint8_t)font8x8_basic[ch][py / 2];
        for (unsigned int px = 0; px < CELL_W; ++px) {
            pixel(MARGIN + x * CELL_W + px, MARGIN + y * CELL_H + py,
                  (bits & (1u << px)) ? foreground : background);
        }
    }
}

static void cursor(void) {
    if (column == columns) { return; }
    for (unsigned int x = 0; x < CELL_W; ++x) {
        pixel(MARGIN + column * CELL_W + x, MARGIN + row * CELL_H + CELL_H - 2, foreground);
    }
}

static void newline(void) {
    column = 0;
    if (++row < rows) { return; }
    row = rows - 1;
    /* Redraw from RAM; reading device memory to scroll is slow and unnecessary. */
    for (unsigned int y = 0; y < rows; ++y) {
        for (unsigned int x = 0; x < columns; ++x) {
            cells[y][x] = y + 1 < rows ? cells[y + 1][x] : ' ';
            draw(x, y);
        }
    }
}

bool screen_init(const struct boot_framebuffer *fb) {
    if (ready || !framebuffer_format_valid(fb)) { return false; }
    framebuffer = *fb;
    columns = (unsigned int)(fb->width - 2 * MARGIN) / CELL_W;
    rows = (unsigned int)(fb->height - 2 * MARGIN) / CELL_H;
    if (columns > MAX_COLS) { columns = MAX_COLS; }
    if (rows > MAX_ROWS) { rows = MAX_ROWS; }
    foreground = color(0xdce6ea);
    background = color(0x101418);
    for (unsigned int y = 0; y < fb->height; ++y) {
        for (unsigned int x = 0; x < fb->width; ++x) { pixel(x, y, background); }
    }
    for (unsigned int y = 0; y < rows; ++y) {
        for (unsigned int x = 0; x < columns; ++x) { cells[y][x] = ' '; }
    }
    ready = true;
    cursor();
    return true;
}

bool screen_ready(void) { return ready; }

static void restore_console(void) {
    for (unsigned int y = 0; y < framebuffer.height; ++y) {
        for (unsigned int x = 0; x < framebuffer.width; ++x) { pixel(x, y, background); }
    }
    for (unsigned int y = 0; y < rows; ++y) {
        for (unsigned int x = 0; x < columns; ++x) { draw(x, y); }
    }
    graphics = false;
    cursor();
}

struct screen_size screen_size(void) {
    return (struct screen_size){ready ? (unsigned int)framebuffer.width : 0,
                               ready ? (unsigned int)framebuffer.height : 0};
}

static bool enter_graphics(void) {
    const uint64_t flags = arch_irq_save();
    const bool allowed = ready && graphics && !busy;
    if (allowed) { busy = true; }
    arch_irq_restore(flags);
    return allowed;
}

static void leave_drawing(void) {
    const uint64_t flags = arch_irq_save();
    busy = false;
    arch_irq_restore(flags);
}

bool screen_graphics_begin(void) {
    const uint64_t flags = arch_irq_save();
    const bool allowed = ready && !busy && !graphics;
    if (allowed) { graphics = true; }
    arch_irq_restore(flags);
    return allowed;
}

void screen_graphics_end(void) {
    if (!enter_graphics()) { return; }
    restore_console();
    leave_drawing();
}

void screen_fill_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, uint32_t rgb) {
    if (!enter_graphics()) { return; }
    if (x < framebuffer.width && y < framebuffer.height) {
        if (width > framebuffer.width - x) { width = (unsigned int)framebuffer.width - x; }
        if (height > framebuffer.height - y) { height = (unsigned int)framebuffer.height - y; }
        const uint32_t value = color(rgb);
        for (unsigned int py = y; py < y + height; ++py) {
            for (unsigned int px = x; px < x + width; ++px) { pixel(px, py, value); }
        }
    }
    leave_drawing();
}

void screen_draw_text(unsigned int x, unsigned int y, const char *text, unsigned int scale, uint32_t rgb) {
    if (!text || scale == 0 || scale > 4 || !enter_graphics()) { return; }
    const uint32_t value = color(rgb);
    while (*text && x < framebuffer.width && y < framebuffer.height) {
        unsigned char ch = (unsigned char)*text++;
        if (ch < 32 || ch > 126) { ch = '?'; }
        for (unsigned int py = 0; py < 8 * scale && py < framebuffer.height - y; ++py) {
            const uint8_t bits = (uint8_t)font8x8_basic[ch][py / scale];
            for (unsigned int px = 0; px < 8 * scale && px < framebuffer.width - x; ++px) {
                if (bits & (1u << (px / scale))) { pixel(x + px, y + py, value); }
            }
        }
        x += 8 * scale;
    }
    leave_drawing();
}

void screen_putc(char value) {
    uint64_t flags = arch_irq_save();
    if (!ready || busy) { arch_irq_restore(flags); return; }
    busy = true;
    arch_irq_restore(flags);
    if (graphics) { restore_console(); }
    if (column < columns) { draw(column, row); }
    const unsigned char ch = (unsigned char)value;
    if (ch == '\n') {
        newline();
    } else if (ch == '\r') {
        column = 0;
    } else if (ch == '\b') {
        if (column) { --column; }
        else if (row) { --row; column = columns - 1; }
    } else if (ch == '\t') {
        unsigned int count = 4 - column % 4;
        while (count--) {
            if (column == columns) { newline(); }
            cells[row][column] = ' ';
            draw(column++, row);
        }
    } else if (ch >= 32) {
        if (column == columns) { newline(); }
        cells[row][column] = ch <= 126 ? ch : '?';
        draw(column++, row);
    }
    cursor();
    flags = arch_irq_save();
    busy = false;
    arch_irq_restore(flags);
}
