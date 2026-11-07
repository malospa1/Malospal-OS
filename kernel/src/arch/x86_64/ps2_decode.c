#include "ps2_decode.h"
#include <malospal/keyboard.h>

static const char plain[128] = {
    [0x0d] = '\t', [0x0e] = '`', [0x15] = 'q', [0x16] = '1',
    [0x1a] = 'z', [0x1b] = 's', [0x1c] = 'a', [0x1d] = 'w', [0x1e] = '2',
    [0x21] = 'c', [0x22] = 'x', [0x23] = 'd', [0x24] = 'e', [0x25] = '4', [0x26] = '3',
    [0x29] = ' ', [0x2a] = 'v', [0x2b] = 'f', [0x2c] = 't', [0x2d] = 'r', [0x2e] = '5',
    [0x31] = 'n', [0x32] = 'b', [0x33] = 'h', [0x34] = 'g', [0x35] = 'y', [0x36] = '6',
    [0x3a] = 'm', [0x3b] = 'j', [0x3c] = 'u', [0x3d] = '7', [0x3e] = '8',
    [0x41] = ',', [0x42] = 'k', [0x43] = 'i', [0x44] = 'o', [0x45] = '0', [0x46] = '9',
    [0x49] = '.', [0x4a] = '/', [0x4b] = 'l', [0x4c] = ';', [0x4d] = 'p', [0x4e] = '-',
    [0x52] = '\'', [0x54] = '[', [0x55] = '=', [0x5a] = '\r', [0x5b] = ']', [0x5d] = '\\',
    [0x66] = '\b', [0x76] = 0x1b, [0x79] = '+', [0x7b] = '-', [0x7c] = '*',
};
static const char shifted[128] = {
    [0x0e] = '~', [0x16] = '!', [0x1e] = '@', [0x25] = '$', [0x26] = '#', [0x2e] = '%',
    [0x36] = '^', [0x3d] = '&', [0x3e] = '*', [0x41] = '<', [0x45] = ')', [0x46] = '(',
    [0x49] = '>', [0x4a] = '?', [0x4c] = ':', [0x4e] = '_', [0x52] = '"',
    [0x54] = '{', [0x55] = '+', [0x5b] = '}', [0x5d] = '|',
};

static enum ps2_result lost(struct ps2_decoder *s) {
    *s = (struct ps2_decoder){0};
    return PS2_LOST;
}

enum ps2_result ps2_decode(struct ps2_decoder *s, uint8_t scan, uint8_t *value) {
    static const uint8_t pause[] = {0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77};
    if (s->pause_index) {
        if (scan != pause[s->pause_index]) { return lost(s); }
        if (++s->pause_index == sizeof(pause)) { s->pause_index = 0; }
        return PS2_NONE;
    }
    if (scan == 0xe1) {
        if (s->extended || s->release) { return lost(s); }
        s->pause_index = 1;
        return PS2_NONE;
    }
    if (scan == 0xe0) {
        if (s->extended || s->release) { return lost(s); }
        s->extended = true;
        return PS2_NONE;
    }
    if (scan == 0xf0) {
        if (s->release) { return lost(s); }
        s->release = true;
        return PS2_NONE;
    }
    if (scan == 0 || scan == 0xff || scan == 0xaa || scan == 0xfc || scan == 0xfd ||
        scan == 0xfa || scan == 0xfe) { return lost(s); }
    const bool extended = s->extended, released = s->release;
    s->extended = s->release = false;
    if (!extended && (scan == 0x12 || scan == 0x59)) {
        if (scan == 0x12) { s->left_shift = !released; }
        else { s->right_shift = !released; }
        return PS2_NONE;
    }
    if (scan == 0x14 || scan == 0x11) {
        bool *held = scan == 0x14 ? (extended ? &s->right_ctrl : &s->left_ctrl) :
                                   (extended ? &s->right_alt : &s->left_alt);
        *held = !released;
        return PS2_NONE;
    }
    if (!extended && scan == 0x58) {
        if (!released && !s->caps_down) { s->caps = !s->caps; }
        s->caps_down = !released;
        return PS2_NONE;
    }
    if (released) { return PS2_NONE; }
    uint8_t ch = 0;
    if (extended) {
        /* Fake Shift in Print Screen must never alter the real Shift state. */
        if (scan == 0x5a) { ch = '\r'; }
        else if (scan == 0x4a) { ch = '/'; }
        else if (scan == 0x71) { ch = 0x7f; }
        else if (scan == 0x75) { ch = KEY_UP; }
        else if (scan == 0x72) { ch = KEY_DOWN; }
        else if (scan == 0x6b) { ch = KEY_LEFT; }
        else if (scan == 0x74) { ch = KEY_RIGHT; }
    } else if (scan < sizeof(plain)) {
        ch = (uint8_t)plain[scan];
        const bool shift = s->left_shift || s->right_shift;
        if (ch >= 'a' && ch <= 'z') {
            if (s->left_ctrl || s->right_ctrl) { ch = ch - 'a' + 1; }
            else if (shift != s->caps) { ch -= 'a' - 'A'; }
        } else if (shift && shifted[scan]) { ch = (uint8_t)shifted[scan]; }
    }
    if (!ch) { return PS2_NONE; }
    *value = (s->left_alt || s->right_alt) ? 0x1b : ch;
    return PS2_BYTE;
}
