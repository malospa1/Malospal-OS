#ifndef MALOSPAL_USB_HID_H
#define MALOSPAL_USB_HID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hid_keyboard {
    uint8_t keys[6], modifiers, repeat;
    bool caps, num, blocked;
    uint64_t repeat_at;
};
typedef void (*hid_emit)(void *context, uint8_t key);
/* False means invalid/rollover report: caller must discard its pending input. */
bool hid_report(struct hid_keyboard *kbd, const uint8_t *report, size_t length,
                uint64_t ticks, hid_emit emit, void *context);
void hid_repeat(struct hid_keyboard *kbd, uint64_t ticks, hid_emit emit, void *context);

struct usb_keyboard_config {
    uint8_t configuration, interface, endpoint, interval;
    uint16_t packet;
};
bool usb_keyboard_config(const uint8_t *bytes, size_t length, struct usb_keyboard_config *result);
#endif
