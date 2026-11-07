#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malospal/keyboard.h>
#include "../src/usb/hid.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "HID failure %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t output[64];
static unsigned int count;
static void emit(void *context, uint8_t key) { CHECK(context == output && count < sizeof(output)); output[count++] = key; }
static void report(struct hid_keyboard *kbd, uint8_t mods, uint8_t key, uint64_t time) {
    const uint8_t bytes[8] = {mods, 0, key};
    CHECK(hid_report(kbd, bytes, sizeof(bytes), time, emit, output));
}
int main(void) {
    struct hid_keyboard kbd = {0};
    report(&kbd, 0, 4, 0); CHECK(count == 1 && output[0] == 'a');
    report(&kbd, 0, 4, 1); CHECK(count == 1);
    hid_repeat(&kbd, 49, emit, output); CHECK(count == 1);
    hid_repeat(&kbd, 50, emit, output); CHECK(count == 2 && output[1] == 'a');
    report(&kbd, 0, 0, 51); hid_repeat(&kbd, 100, emit, output); CHECK(count == 2);
    report(&kbd, 0x20, 4, 100); CHECK(output[count - 1] == 'A'); report(&kbd, 0, 0, 100);
    report(&kbd, 0, 57, 100); report(&kbd, 0, 57, 101); report(&kbd, 0, 0, 102);
    report(&kbd, 0, 5, 103); CHECK(output[count - 1] == 'B'); report(&kbd, 0, 0, 104);
    report(&kbd, 2, 6, 105); CHECK(output[count - 1] == 'c'); report(&kbd, 0, 0, 106);
    report(&kbd, 0x10, 24, 107); CHECK(output[count - 1] == 21); report(&kbd, 0, 0, 108);
    report(&kbd, 0, 82, 109); CHECK(output[count - 1] == KEY_UP); report(&kbd, 0, 0, 110);
    report(&kbd, 0, 88, 111); CHECK(output[count - 1] == '\r'); report(&kbd, 0, 0, 112);
    const unsigned int before = count;
    report(&kbd, 4, 4, 113); CHECK(count == before); report(&kbd, 0, 0, 114);
    uint8_t rollover[8] = {0, 0, 1, 1, 1, 1, 1, 1};
    CHECK(!hid_report(&kbd, rollover, 8, 200, emit, output));
    report(&kbd, 0, 4, 201); CHECK(count == before);
    hid_repeat(&kbd, 1000, emit, output); CHECK(count == before);
    report(&kbd, 0, 0, 1001); report(&kbd, 0, 5, 1002); CHECK(count == before + 1);
    uint8_t duplicate[8] = {0, 0, 4, 4};
    CHECK(!hid_report(&kbd, duplicate, 8, 1003, emit, output));
    report(&kbd, 0, 0, 1004);
    for (size_t size = 0; size < 8; ++size) { CHECK(!hid_report(&kbd, rollover, size, 0, emit, output)); }
    report(&kbd, 0, 0, 0);
    count = 0;
    for (uint8_t usage = 45; usage <= 56; ++usage) {
        report(&kbd, 2, usage, 0); report(&kbd, 0, 0, 0);
    }
    CHECK(count == 12 && memcmp(output, "_+{}|~:\"~<>?", 12) == 0);

    uint8_t descriptor[] = {9,2,34,0,1,1,0,0x80,50, 9,4,0,0,1,3,1,1,0,
                           9,0x21,0x11,1,0,1,0x22,63,0, 7,5,0x81,3,8,0,10};
    struct usb_keyboard_config config;
    CHECK(usb_keyboard_config(descriptor, sizeof(descriptor), &config));
    CHECK(config.configuration == 1 && config.endpoint == 0x81 && config.packet == 8 && config.interval == 10);
    for (size_t size = 0; size < sizeof(descriptor); ++size) { CHECK(!usb_keyboard_config(descriptor, size, &config)); }
    descriptor[9] = 0; CHECK(!usb_keyboard_config(descriptor, sizeof(descriptor), &config)); descriptor[9] = 9;
    descriptor[16] = 2; CHECK(!usb_keyboard_config(descriptor, sizeof(descriptor), &config)); descriptor[16] = 1;
    descriptor[29] = 1; CHECK(!usb_keyboard_config(descriptor, sizeof(descriptor), &config)); descriptor[29] = 0x81;
    descriptor[33] = 0; CHECK(!usb_keyboard_config(descriptor, sizeof(descriptor), &config));
    /* Arbitrary descriptor bytes and all lengths: bounded parser under UBSan. */
    uint32_t seed = 1;
    for (unsigned int i = 0; i < 100000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        descriptor[seed % sizeof(descriptor)] = (uint8_t)(seed >> 16);
        (void)usb_keyboard_config(descriptor, (seed >> 8) % (sizeof(descriptor) + 1), &config);
    }
    puts("PASS: USB HID press/release, repeat, modifiers, rollover recovery and bounded descriptor parser.");
    return 0;
}
