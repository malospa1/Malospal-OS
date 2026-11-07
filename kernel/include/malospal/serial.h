#ifndef MALOSPAL_SERIAL_H
#define MALOSPAL_SERIAL_H

#include <stdbool.h>
#include <stdint.h>

#define SERIAL_RX_CAPACITY 1024u

bool serial_init(void);
void serial_write_byte(char value);

enum serial_rx_result { SERIAL_RX_EMPTY, SERIAL_RX_BYTE, SERIAL_RX_LOST };
struct serial_stats {
    uint64_t received, dropped, errors, interrupts;
    unsigned int queued;
    bool faulted;
};

/* Once, after serial_init() and PIC setup, with IF clear. Enables IRQ4. */
bool serial_rx_init(void);
bool serial_rx_ready(void);
/* Single CPU. These calls preserve IF. Loss precedes any surviving bytes. */
enum serial_rx_result serial_read(uint8_t *value);
struct serial_stats serial_get_stats(void);

#endif
