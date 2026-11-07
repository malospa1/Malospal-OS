#ifndef MALOSPAL_USB_H
#define MALOSPAL_USB_H
#include <malospal/boot.h>
#include <malospal/keyboard.h>
struct usb_stats {
    unsigned int controllers, keyboards, unsupported, failed, queued;
    uint64_t reports, decoded, errors, dropped;
};
/* Once, after VMM/PIT, IF set; enumerates USB 2 boot keyboards on xHCI root ports. */
bool usb_init(const struct boot_info *boot);
/* Bounded nonblocking poll, no allocation; preserves IF. Device is a stable input source ID. */
enum keyboard_rx_result usb_read_key(uint8_t *value, unsigned int *device);
bool usb_ready(void);
struct usb_stats usb_get_stats(void);
#endif
