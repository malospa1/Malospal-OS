#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/serial.h>
#include "../src/arch/x86_64/serial_irq.h"

#define REQUIRE(test) do { \
    if (!(test)) { \
        fprintf(stderr, "Serial failure at %d: %s\n", __LINE__, #test); \
        exit(1); \
    } \
} while (0)

static bool enabled, pic_ready, loopback, dlab, masked = true, stuck;
static bool present = true, loop_pending, loop_stalled, loop_bad;
static bool stale_rx, rx_stalled;
static unsigned int startup_reads;
static unsigned int loop_delay, loop_tx_delay, loop_polls;
static uint8_t loop_error;
static uint8_t loop_value, ier, fcr, rx_error, reason = 4;
static uint8_t input[2048];
static size_t input_pos, input_end;
static char output[256];
static size_t output_length;

void screen_putc(char value) { (void)value; }

bool arch_irq_enabled(void) { return enabled; }
uint64_t arch_irq_save(void) { const uint64_t old = enabled ? 512 : 0; enabled = false; return old; }
void arch_irq_restore(uint64_t flags) { enabled = (flags & 512) != 0; }
bool pic_is_ready(void) { return pic_ready; }
bool pic_mask(uint8_t irq) { REQUIRE(!enabled && irq == 4); masked = true; return true; }
bool pic_unmask(uint8_t irq) { REQUIRE(!enabled && irq == 4); masked = false; return true; }

void outb(uint16_t port, uint8_t value) {
    REQUIRE(port >= 0x3f8 && port <= 0x3fe);
    if (!present) { return; }
    if (port == 0x3fb) { dlab = (value & 0x80) != 0; }
    if (port == 0x3fc) { loopback = (value & 0x10) != 0; }
    if (port == 0x3f9 && !dlab) { ier = value; }
    if (port == 0x3fa) {
        fcr = value;
        if (value & 2) { loop_pending = false; }
    }
    if (port == 0x3f8 && !dlab) {
        if (loopback) {
            loop_value = value ^ (loop_bad ? 1 : 0);
            loop_pending = true;
            loop_polls = 0;
        } else {
            REQUIRE(output_length < sizeof(output));
            output[output_length++] = (char)value;
        }
    }
}

uint8_t inb(uint16_t port) {
    REQUIRE(!enabled);
    if (!present) { return 0xff; }
    if (port == 0x3fd && loopback) {
        REQUIRE(++loop_polls <= 1000000);
        return 0x20 | (loop_polls > loop_tx_delay ? 0x40 : 0) |
               (loop_pending && !loop_stalled && loop_polls > loop_delay ? 1 : 0) | loop_error;
    }
    if (port == 0x3f8 && loopback) {
        REQUIRE(loop_pending && !loop_stalled && loop_polls > loop_delay);
        loop_pending = false;
        return loop_value;
    }
    if (port == 0x3fa) {
        return stuck ? 6 : rx_error ? 6 : input_pos < input_end ? reason : 1;
    }
    if (port == 0x3fd) {
        const uint8_t value = 0x20 | rx_error |
                              (stale_rx || rx_stalled || input_pos < input_end ? 1 : 0);
        rx_error = 0;
        return value;
    }
    if (port == 0x3fe) { return 0; }
    if (port == 0x3f8 && (stale_rx || rx_stalled)) {
        REQUIRE(++startup_reads <= 4097);
        stale_rx = false;
        return 0;
    }
    REQUIRE(port == 0x3f8 && input_pos < input_end);
    return input[input_pos++];
}

static void inject(size_t count, uint8_t value) {
    REQUIRE(input_pos == input_end && count <= sizeof(input));
    memset(input, value, count);
    input_pos = 0;
    input_end = count;
}

static void irq(void) {
    const uint64_t flags = arch_irq_save();
    serial_on_irq();
    arch_irq_restore(flags);
}

int main(void) {
    uint8_t value = 0;
    REQUIRE(!serial_rx_init() && serial_read(&value) == SERIAL_RX_EMPTY);
    loop_delay = 32;
    REQUIRE(debug_init() && loop_polls == 33 && !loopback && !loop_pending);
    REQUIRE(ier == 0 && fcr == 0xc7);
    loop_tx_delay = 64;
    REQUIRE(debug_init() && loop_polls == 65 && !loopback && !loop_pending);
    loop_tx_delay = 0;
    loop_bad = true;
    REQUIRE(!debug_init() && !loopback && !loop_pending);
    debug_write("hidden");
    REQUIRE(output_length == 0);
    loop_bad = false;
    loop_stalled = true;
    REQUIRE(!debug_init() && loop_polls > 0 && !loopback && !loop_pending);
    loop_stalled = false;
    loop_error = 4;
    REQUIRE(!debug_init() && !loopback && !loop_pending);
    loop_error = 0;
    present = false;
    REQUIRE(!debug_init() && !serial_rx_init());
    present = true;
    loop_delay = 0;
    REQUIRE(debug_init() && !serial_rx_init());
    pic_ready = true;
    enabled = true;
    REQUIRE(!serial_rx_init());
    enabled = false;
    rx_stalled = true;
    REQUIRE(!serial_rx_init() && masked && ier == 0 && !enabled);
    REQUIRE(startup_reads == 4096 && !serial_rx_ready());
    REQUIRE(serial_read(&value) == SERIAL_RX_EMPTY && serial_get_stats().received == 0);
    rx_stalled = false;
    stale_rx = true;
    inject(16, 0xae);
    rx_error = 2;
    REQUIRE(serial_rx_init() && !masked && ier == 5 && fcr == 7 && !enabled);
    REQUIRE(serial_rx_ready() && startup_reads == 4097 && input_pos == input_end);
    REQUIRE(serial_read(&value) == SERIAL_RX_EMPTY);
    REQUIRE(serial_get_stats().received == 0 && serial_get_stats().errors == 0);
    REQUIRE(!serial_rx_init());
    enabled = true;
    REQUIRE(serial_read(NULL) == SERIAL_RX_EMPTY && enabled);
    for (unsigned int round = 0; round < 4; ++round) {
        reason = round & 1 ? 12 : 4;
        inject(700, (uint8_t)('a' + round));
        irq();
        REQUIRE(enabled && serial_get_stats().queued == 700);
        for (unsigned int i = 0; i < 700; ++i) {
            REQUIRE(serial_read(&value) == SERIAL_RX_BYTE && value == 'a' + round && enabled);
        }
        REQUIRE(serial_read(&value) == SERIAL_RX_EMPTY);
    }
    inject(SERIAL_RX_CAPACITY + 3, 'z');
    irq();
    REQUIRE(serial_get_stats().dropped == SERIAL_RX_CAPACITY);
    REQUIRE(serial_read(&value) == SERIAL_RX_LOST);
    for (unsigned int i = 0; i < 3; ++i) {
        REQUIRE(serial_read(&value) == SERIAL_RX_BYTE && value == 'z');
    }
    REQUIRE(serial_read(&value) == SERIAL_RX_EMPTY);
    for (unsigned int bit = 1; bit <= 4; ++bit) {
        inject(1, 'x');
        rx_error = (uint8_t)(1u << bit);
        irq();
        REQUIRE(serial_read(&value) == SERIAL_RX_LOST);
        REQUIRE(serial_read(&value) == SERIAL_RX_EMPTY);
    }
    /* Error accounting must also happen when the transmit path reads LSR. */
    inject(1, 'x');
    rx_error = 2;
    debug_write("ok\n");
    REQUIRE(enabled && output_length == 4 && !memcmp(output, "ok\r\n", 4));
    REQUIRE(serial_read(&value) == SERIAL_RX_LOST && serial_get_stats().errors == 5);
    inject(1, 'q');
    debug_write(".");
    REQUIRE(serial_read(&value) == SERIAL_RX_BYTE && value == 'q');
    irq(); /* A spurious IRQ with no UART cause returns normally. */
    REQUIRE(!serial_get_stats().faulted);
    stuck = true;
    irq();
    REQUIRE(enabled && masked && ier == 0 && serial_get_stats().faulted);
    REQUIRE(serial_read(&value) == SERIAL_RX_LOST && !serial_rx_init());
    puts("PASS: UART probe, bounded startup drain, IRQ/timeout, ring wrap, loss/errors, IF and stuck-device bound.");
    return 0;
}
