#include <malospal/keyboard.h>
#include <malospal/timer.h>
#include "hid.h"

static bool contains(const uint8_t *keys, uint8_t key) {
    for (unsigned int i = 0; i < 6; ++i) { if (keys[i] == key) { return true; } }
    return false;
}

static uint8_t translate(const struct hid_keyboard *kbd, uint8_t usage) {
    const bool shift = (kbd->modifiers & 0x22) != 0, ctrl = (kbd->modifiers & 0x11) != 0;
    if (kbd->modifiers & 0xcc) { return 0; } /* Alt/GUI shortcuts are not text. */
    if (usage >= 4 && usage <= 29) {
        if (ctrl) { return usage - 3; }
        return (uint8_t)((shift != kbd->caps ? 'A' : 'a') + usage - 4);
    }
    if (ctrl) { return 0; }
    if (usage >= 30 && usage <= 39) { return (uint8_t)(shift ? "!@#$%^&*()" : "1234567890")[usage - 30]; }
    if (usage >= 45 && usage <= 56) { return (uint8_t)(shift ? "_+{}|~:\"~<>?" : "-=[]\\#;'`,./")[usage - 45]; }
    switch (usage) {
        case 40: case 88: return '\r';
        case 41: return 27;
        case 42: return '\b';
        case 43: return '\t';
        case 44: return ' ';
        case 76: return 127;
        case 79: return KEY_RIGHT;
        case 80: return KEY_LEFT;
        case 81: return KEY_DOWN;
        case 82: return KEY_UP;
        case 84: return '/';
        case 85: return '*';
        case 86: return '-';
        case 87: return '+';
        default: break;
    }
    if (kbd->num && usage >= 89 && usage <= 99) { return (uint8_t)"1234567890."[usage - 89]; }
    return 0;
}

bool hid_report(struct hid_keyboard *kbd, const uint8_t *report, size_t length,
                uint64_t ticks, hid_emit emit, void *context) {
    if (!kbd || !report || !emit) { return false; }
    bool valid = length == 8 && report[1] == 0;
    if (valid) {
        for (unsigned int i = 2; i < 8; ++i) {
            if (report[i] && report[i] <= 3) { valid = false; }
            for (unsigned int j = 2; j < i; ++j) {
                if (report[i] && report[i] == report[j]) { valid = false; }
            }
        }
    }
    if (!valid) {
        kbd->blocked = true;
        kbd->repeat = 0;
        return false;
    }
    bool empty = true;
    for (unsigned int i = 2; i < 8; ++i) { if (report[i]) { empty = false; } }
    if (kbd->blocked) {
        if (!empty || report[0]) { return true; }
        kbd->blocked = false;
        for (unsigned int i = 0; i < 6; ++i) { kbd->keys[i] = 0; }
    }
    kbd->modifiers = report[0];
    if (!contains(report + 2, kbd->repeat)) { kbd->repeat = 0; }
    for (unsigned int i = 2; i < 8; ++i) {
        const uint8_t key = report[i];
        if (!key || contains(kbd->keys, key)) { continue; }
        if (key == 57) { kbd->caps = !kbd->caps; }
        else if (key == 83) { kbd->num = !kbd->num; }
        else {
            const uint8_t value = translate(kbd, key);
            if (value) { emit(context, value); kbd->repeat = key; kbd->repeat_at = ticks + TIMER_HZ / 2; }
        }
    }
    for (unsigned int i = 0; i < 6; ++i) { kbd->keys[i] = report[i + 2]; }
    return true;
}

void hid_repeat(struct hid_keyboard *kbd, uint64_t ticks, hid_emit emit, void *context) {
    if (kbd->repeat && !kbd->blocked && ticks - kbd->repeat_at < (UINT64_C(1) << 63)) {
        const uint8_t value = translate(kbd, kbd->repeat);
        if (value) { emit(context, value); }
        kbd->repeat_at = ticks + TIMER_HZ / 20;
    }
}

bool usb_keyboard_config(const uint8_t *b, size_t length, struct usb_keyboard_config *result) {
    if (!b || !result || length < 9 || length > 4096 || b[0] != 9 || b[1] != 2 ||
        ((unsigned int)b[2] | (unsigned int)b[3] << 8) != length || !b[5]) { return false; }
    bool keyboard = false, found = false;
    uint8_t interface = 0;
    struct usb_keyboard_config config = {0};
    for (size_t at = 9; at < length;) {
        if (length - at < 2 || b[at] < 2 || b[at] > length - at) { return false; }
        const uint8_t *d = b + at;
        if (d[1] == 4) {
            if (d[0] < 9) { return false; }
            keyboard = d[3] == 0 && d[5] == 3 && d[6] == 1 && d[7] == 1;
            interface = d[2];
        } else if (d[1] == 5) {
            if (d[0] < 7) { return false; }
            const unsigned int packet = d[4] | (unsigned int)d[5] << 8;
            if (!found && keyboard && (d[2] & 0xf0) == 0x80 && (d[2] & 15) &&
                d[3] == 3 && packet >= 8 && packet <= 1024 && d[6]) {
                config = (struct usb_keyboard_config){b[5], interface, d[2], d[6], (uint16_t)packet};
                found = true;
            }
        }
        at += d[0];
    }
    if (found) { *result = config; }
    return found;
}
