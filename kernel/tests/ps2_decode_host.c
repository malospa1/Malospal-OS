#include <stdio.h>
#include <stdlib.h>
#include <malospal/keyboard.h>
#include "../src/arch/x86_64/ps2_decode.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "PS/2 decode failure %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static struct ps2_decoder state;
static uint8_t output;
static enum ps2_result feed(uint8_t scan) { return ps2_decode(&state, scan, &output); }
static void none(uint8_t scan) { CHECK(feed(scan) == PS2_NONE); }
static void byte(uint8_t scan, uint8_t expected) { CHECK(feed(scan) == PS2_BYTE && output == expected); }
static void up(uint8_t scan) { none(0xf0); none(scan); }

int main(void) {
    static const uint8_t letters[] = {0x1c,0x32,0x21,0x23,0x24,0x2b,0x34,0x33,0x43,0x3b,0x42,0x4b,0x3a,
                                     0x31,0x44,0x4d,0x15,0x2d,0x1b,0x2c,0x3c,0x2a,0x1d,0x22,0x35,0x1a};
    for (unsigned int i = 0; i < sizeof(letters); ++i) { byte(letters[i], 'a' + i); up(letters[i]); }
    byte(0x1c, 'a'); byte(0x1c, 'a'); /* Typematic make codes are printable. */
    none(0x12); none(0x59); up(0x12); byte(0x1c, 'A'); up(0x59); byte(0x1c, 'a');
    none(0x58); none(0x58); byte(0x1c, 'A'); up(0x58);
    none(0x12); byte(0x1c, 'a'); up(0x12);
    none(0x58); up(0x58); byte(0x1c, 'a');
    static const uint8_t symbols[] = {0x0e,0x16,0x1e,0x26,0x25,0x2e,0x36,0x3d,0x3e,0x46,0x45,
                                     0x4e,0x55,0x54,0x5b,0x5d,0x4c,0x52,0x41,0x49,0x4a};
    const char *plain = "`1234567890-=[]\\;',./", *shift = "~!@#$%^&*()_+{}|:\"<>?";
    for (unsigned int i = 0; i < sizeof(symbols); ++i) { byte(symbols[i], plain[i]); }
    none(0x12);
    for (unsigned int i = 0; i < sizeof(symbols); ++i) { byte(symbols[i], shift[i]); }
    up(0x12);
    byte(0x5a, '\r'); byte(0x66, '\b'); byte(0x0d, '\t'); byte(0x29, ' '); byte(0x76, 0x1b);
    none(0x14); byte(0x3c, 0x15); up(0x14);
    none(0xe0); none(0x14); byte(0x3c, 0x15); none(0xe0); up(0x14);
    none(0x11); byte(0x1c, 0x1b); up(0x11);
    none(0xe0); none(0x11); byte(0x1c, 0x1b); none(0xe0); up(0x11);
    none(0xe0); byte(0x5a, '\r'); none(0xe0); up(0x5a);
    none(0xe0); byte(0x4a, '/'); none(0xe0); byte(0x71, 0x7f);
    static const uint8_t special[] = {0xe1,0x14,0x77,0xe1,0xf0,0x14,0xf0,0x77,
                                    0xe0,0x12,0xe0,0x7c,0xe0,0xf0,0x7c,0xe0,0xf0,0x12,
                                    0x05,0xf0,0x05};
    for (unsigned int i = 0; i < sizeof(special); ++i) { none(special[i]); }
    const uint8_t arrows[] = {0x75, 0x72, 0x6b, 0x74};
    for (unsigned int i = 0; i < sizeof(arrows); ++i) {
        none(0xe0); byte(arrows[i], (uint8_t)(KEY_UP + i)); none(0xe0); up(arrows[i]);
    }
    byte(0x1c, 'a');
    none(0xe0); CHECK(feed(0xe0) == PS2_LOST);
    none(0xf0); CHECK(feed(0xf0) == PS2_LOST);
    none(0xe1); CHECK(feed(0x1c) == PS2_LOST);
    const uint8_t invalid[] = {0,0xff,0xaa,0xfa,0xfe,0xfc,0xfd};
    for (unsigned int i = 0; i < sizeof(invalid); ++i) {
        none(0x12); CHECK(feed(invalid[i]) == PS2_LOST); byte(0x1c, 'a');
    }
    uint32_t random = 12345;
    for (unsigned int i = 0; i < 100000; ++i) {
        random = random * 1664525u + 1013904223u;
        const enum ps2_result result = feed((uint8_t)(random >> 24));
        CHECK(state.pause_index < 8 && (result != PS2_BYTE || (output > 0 && output <= KEY_RIGHT)));
    }
    puts("PASS: PS/2 set 2, US keys, modifiers, repeats, E0/E1, malformed sequences and fuzz.");
    return 0;
}
