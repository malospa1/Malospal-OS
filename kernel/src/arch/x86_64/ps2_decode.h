#ifndef MALOSPAL_PS2_DECODE_H
#define MALOSPAL_PS2_DECODE_H

#include <stdbool.h>
#include <stdint.h>

struct ps2_decoder {
    bool extended, release, left_shift, right_shift, left_ctrl, right_ctrl;
    bool left_alt, right_alt, caps, caps_down;
    uint8_t pause_index;
};

enum ps2_result { PS2_NONE, PS2_BYTE, PS2_LOST };
/* Native set 2, US ASCII. Zero-initialize; reset on lost bytes. No I/O. */
enum ps2_result ps2_decode(struct ps2_decoder *state, uint8_t scan, uint8_t *value);

#endif
