#include <malospal/arch.h>
#include <malospal/keyboard.h>
#include <malospal/serial.h>
#include <malospal/input.h>
#include "game_input.h"

bool game_read_key(uint8_t *value, bool *lost) {
    unsigned int source;
    *value = 0;
    const enum serial_rx_result result = input_read(value, &source, true);
    *lost = result == SERIAL_RX_LOST;
    return result != SERIAL_RX_EMPTY;
}

void game_discard_input(void) {
    uint8_t key;
    bool lost;
    for (unsigned int i = 0; i < SERIAL_RX_CAPACITY + 2 * KEYBOARD_RX_CAPACITY; ++i) {
        if (!game_read_key(&key, &lost)) { break; }
    }
}

void game_wait_tick(void) {
    const uint64_t flags = arch_irq_save();
    arch_irq_wait();
    arch_irq_restore(flags);
}
