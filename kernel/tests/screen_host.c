#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malospal/arch.h>
#include <malospal/screen.h>
#include "../vendor/font8x8/font8x8_basic.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "Screen failure at %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned char memory[32768];
static struct boot_framebuffer fb;
static bool enabled = true, reenter;
uint64_t arch_irq_save(void) { uint64_t f = enabled ? 512 : 0; enabled = false; return f; }
void arch_irq_restore(uint64_t f) {
    enabled = (f & 512) != 0;
    if (reenter) { reenter = false; screen_putc('!'); }
}

static void write_text(const char *text) {
    while (*text) { screen_putc(*text++); REQUIRE(enabled); }
}

static uint32_t color(uint32_t rgb) {
    return ((rgb >> 16) & 255) << fb.red_shift | ((rgb >> 8) & 255) << fb.green_shift |
           (rgb & 255) << fb.blue_shift;
}

static void glyph(unsigned int x, unsigned int y, unsigned char ch) {
    for (unsigned int py = 0; py < 16; ++py) {
        for (unsigned int px = 0; px < 8; ++px) {
            const unsigned char *p = memory + 64 + (16 + y * 16 + py) * fb.pitch +
                                     (16 + x * 8 + px) * (fb.bpp / 8);
            uint32_t value = 0;
            for (unsigned int i = 0; i < fb.bpp / 8; ++i) { value |= (uint32_t)p[i] << (i * 8); }
            REQUIRE(value == color((font8x8_basic[ch][py / 2] & (1u << px)) ? 0xdce6ea : 0x101418));
        }
    }
}

static void check_boot_validation(void) {
    struct boot_info boot = {0};
    boot.direct_map_offset = UINT64_C(0xffff800000000000);
    boot.framebuffer = fb;
    boot.framebuffer.physical_address = 0xe0000123;
    boot.framebuffer.address = boot.direct_map_offset + boot.framebuffer.physical_address;
    boot.memory_region_count = 1;
    boot.regions[0] = (struct boot_memory_region){0xe0000000, 0x10000, BOOT_MEMORY_FRAMEBUFFER};
    REQUIRE(boot_framebuffer_valid(&boot));
    boot.regions[0].type = BOOT_MEMORY_USABLE;
    REQUIRE(!boot_framebuffer_valid(&boot));
    boot.regions[0].type = BOOT_MEMORY_FRAMEBUFFER;
    boot.regions[0].length = 4096;
    REQUIRE(!boot_framebuffer_valid(&boot));
    boot.regions[0].length = 0x10000;
    boot.memory_region_count = 2;
    boot.regions[1] = (struct boot_memory_region){0xe0000000, 4096, BOOT_MEMORY_KERNEL_MODULES};
    REQUIRE(!boot_framebuffer_valid(&boot));
    boot.memory_region_count = 1;
    ++boot.framebuffer.address;
    REQUIRE(!boot_framebuffer_valid(&boot));
    REQUIRE(!boot_framebuffer_valid(NULL));
    struct boot_framebuffer bad = fb;
    bad.pitch = UINT64_MAX;
    REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.bpp = 16; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.red_shift = fb.green_shift; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.blue_shift = 31; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.red_size = 9; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.address = UINT64_MAX - 16; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.width = 0; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.height = 47; REQUIRE(!framebuffer_format_valid(&bad));
    bad = fb; bad.pitch = fb.width * (fb.bpp / 8) - 1; REQUIRE(!framebuffer_format_valid(&bad));
    REQUIRE(!screen_init(&bad) && !screen_ready());
}

int main(int argc, char **argv) {
    REQUIRE(argc == 2);
    const unsigned int bpp = (unsigned int)strtoul(argv[1], NULL, 10);
    REQUIRE(bpp == 24 || bpp == 32);
    memset(memory, 0xa5, sizeof(memory));
    fb = (struct boot_framebuffer){
        .address = (uintptr_t)(memory + 64), .width = 80, .height = 80, .pitch = 80 * (bpp / 8) + 13,
        .bpp = (uint16_t)bpp, .red_size = 8, .green_size = 8, .blue_size = 8,
        .red_shift = bpp == 24 ? 0 : 16, .green_shift = 8, .blue_shift = bpp == 24 ? 16 : 0,
    };
    screen_putc('X');
    REQUIRE(enabled && !screen_ready() && memory[64] == 0xa5);
    check_boot_validation();
    REQUIRE(screen_init(&fb) && screen_ready() && !screen_init(&fb));
    reenter = true;
    write_text("A");
    glyph(0, 0, 'A');
    write_text("\b \bB");
    glyph(0, 0, 'B');
    write_text("\rABCDEF\bZ\nG");
    glyph(5, 0, 'Z'); glyph(0, 1, 'G');
    write_text("\r\tH");
    glyph(0, 1, ' '); glyph(3, 1, ' '); glyph(4, 1, 'H');
    write_text("\n\n\n1\n2\n3\n4");
    glyph(0, 0, '2'); glyph(0, 1, '3'); glyph(0, 2, '4');
    write_text("\x80"); glyph(1, 2, '?');
    REQUIRE(screen_size().width == 80 && screen_size().height == 80);
    REQUIRE(screen_graphics_begin() && !screen_graphics_begin() && enabled);
    screen_fill_rect(0, 0, UINT32_MAX, UINT32_MAX, 0x123456);
    screen_fill_rect(UINT32_MAX, UINT32_MAX, 10, 10, 0xffffff);
    const uint32_t expected = color(0x123456);
    for (unsigned int i = 0; i < bpp / 8; ++i) { REQUIRE(memory[64 + i] == (uint8_t)(expected >> (i * 8))); }
    screen_draw_text(79, 79, "edge", 4, 0xffffff);
    screen_draw_text(UINT32_MAX, UINT32_MAX, "outside", 1, 0xffffff);
    screen_draw_text(0, 0, NULL, 1, 0xffffff);
    screen_draw_text(0, 0, "ignored", UINT32_MAX, 0xffffff);
    screen_graphics_end();
    REQUIRE(enabled);
    glyph(0, 0, '2'); glyph(0, 1, '3'); glyph(0, 2, '4'); glyph(1, 2, '?');
    REQUIRE(screen_graphics_begin());
    screen_fill_rect(0, 0, 80, 80, 0);
    screen_putc('Z'); /* Diagnostics take the screen back from a game. */
    glyph(0, 0, '2'); glyph(2, 2, 'Z');
    REQUIRE(screen_graphics_begin());
    enabled = false;
    screen_fill_rect(0, 0, 1, 1, 0xff0000);
    screen_draw_text(16, 16, "A", 2, 0xffffff);
    screen_graphics_end(); REQUIRE(!enabled);
    enabled = true;
    for (unsigned int i = 0; i < 64; ++i) { REQUIRE(memory[i] == 0xa5); }
    for (uint64_t y = 0; y < fb.height; ++y) {
        for (uint64_t x = fb.width * (bpp / 8); x < fb.pitch; ++x) {
            REQUIRE(memory[64 + y * fb.pitch + x] == 0xa5);
        }
    }
    for (uint64_t i = 64 + fb.pitch * fb.height; i < sizeof(memory); ++i) { REQUIRE(memory[i] == 0xa5); }
    enabled = false; screen_putc('X'); REQUIRE(!enabled);
    printf("PASS: screen %u-bit RGB, pitch/guards, glyphs, scroll, clipped graphics, console restore, IF and validation.\n", bpp);
    return 0;
}
