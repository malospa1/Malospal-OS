#ifndef MALOSPAL_KEYBOARD_H
#define MALOSPAL_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

#define KEYBOARD_RX_CAPACITY 256u

enum keyboard_rx_result { KEYBOARD_RX_EMPTY, KEYBOARD_RX_BYTE, KEYBOARD_RX_LOST };
enum keyboard_key { KEY_UP = 0x80, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
struct keyboard_stats {
    uint64_t received, decoded, dropped, errors, interrupts;
    unsigned int queued;
    bool ready, faulted;
};
struct keyboard_diagnostics { const char *stage; uint8_t status, reply; };

/* Once, after PIC setup, with IF clear. Owns i8042; the mouse port stays disabled. */
bool keyboard_init(void);
/* PIT initialized, IF clear. EC-friendly: no controller reset; allows timer IRQs
 * while IRQ1 stays masked, bounded 500 ms command waits. Still verifies set 2. */
bool keyboard_init_timed(void);
struct keyboard_diagnostics keyboard_get_diagnostics(void);
/* Single CPU. Reads and snapshots preserve IF; loss precedes surviving input. */
enum keyboard_rx_result keyboard_read(uint8_t *value);
/* ASCII plus KEY_* arrows. The text-only reader above filters arrows. */
enum keyboard_rx_result keyboard_read_key(uint8_t *value);
struct keyboard_stats keyboard_get_stats(void);

#endif
