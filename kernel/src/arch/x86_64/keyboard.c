#include <malospal/arch.h>
#include <malospal/keyboard.h>
#include <malospal/timer.h>
#include "io.h"
#include "keyboard_irq.h"
#include "pic.h"
#include "ps2_decode.h"

enum { DATA = 0x60, STATUS = 0x64, POLL_LIMIT = 1000000, IRQ_LIMIT = 256 };
static bool attempted, lost;
static uint8_t buffer[KEYBOARD_RX_CAPACITY];
static unsigned int head, tail;
static struct keyboard_stats stats;
static struct ps2_decoder decoder;
static bool timed;
static struct keyboard_diagnostics diagnostic = {"not-started", 0, 0};

static bool poll_pause(unsigned int iteration, uint64_t start) {
    if (timed) {
        if (timer_ticks() - start >= TIMER_HZ / 2) { return false; }
        if ((iteration & 255) == 255) { arch_irq_wait(); }
    }
    __asm__ volatile ("pause");
    return true;
}

_Static_assert((KEYBOARD_RX_CAPACITY & (KEYBOARD_RX_CAPACITY - 1)) == 0, "Keyboard RX ring size");

static unsigned int queued(void) { return (head - tail) & (KEYBOARD_RX_CAPACITY - 1); }

static bool write_port(uint16_t port, uint8_t value) {
    const uint64_t start = timed ? timer_ticks() : 0;
    for (unsigned int i = 0; i < POLL_LIMIT; ++i) {
        const uint8_t status = inb(STATUS);
        diagnostic.status = status;
        if (status == 0xff) { return false; }
        if (!(status & 2)) { outb(port, value); return true; }
        if (!poll_pause(i, start)) { return false; }
    }
    return false;
}

static bool read_reply(uint8_t *value) {
    const uint64_t start = timed ? timer_ticks() : 0;
    for (unsigned int i = 0; i < POLL_LIMIT; ++i) {
        const uint8_t status = inb(STATUS);
        diagnostic.status = status;
        if (status == 0xff) { return false; }
        if (status & 1) {
            const uint8_t data = inb(DATA);
            diagnostic.reply = data;
            if (status & 0xc0) { return false; }
            if (!(status & 0x20)) { *value = data; return true; }
        }
        if (!poll_pause(i, start)) { return false; }
    }
    return false;
}

static bool flush(void) {
    for (unsigned int i = 0; i < IRQ_LIMIT; ++i) {
        const uint8_t status = inb(STATUS);
        if (status == 0xff) { return false; }
        if (!(status & 1)) { return true; }
        (void)inb(DATA);
    }
    return false;
}

static bool config_write(uint8_t value) {
    return write_port(STATUS, 0x60) && write_port(DATA, value);
}

static bool controller_test(uint8_t command, uint8_t expected) {
    uint8_t reply;
    return write_port(STATUS, command) && read_reply(&reply) && reply == expected;
}

static bool keyboard_command(uint8_t value) {
    for (unsigned int retry = 0; retry < 3; ++retry) {
        uint8_t reply;
        if (!write_port(DATA, value) || !read_reply(&reply)) { return false; }
        if (reply == 0xfa) { return true; }
        if (reply != 0xfe) { return false; }
    }
    return false;
}

bool keyboard_init(void) {
    if (attempted || arch_irq_enabled() || !pic_is_ready()) { return false; }
    attempted = true;
    pic_mask(1);
    uint8_t config, reply;
    diagnostic.stage = "controller-config";
    if (!write_port(STATUS, 0xad) || !write_port(STATUS, 0xa7) || !flush() ||
        !write_port(STATUS, 0x20) || !read_reply(&config)) { goto failed; }
    config = (config & (uint8_t)~0x43) | 0x30;
    if (!config_write(config)) { goto failed; }
    /* Laptop ECs may not implement reset/self-test as a discrete 8042 does.
     * Firmware already initialized the controller. Prove communication with
     * keyboard commands and config readback instead of resetting the EC. */
    if (!timed) {
        diagnostic.stage = "controller-selftest";
        if (!controller_test(0xaa, 0x55) || !write_port(STATUS, 0xad) ||
            !write_port(STATUS, 0xa7) || !config_write(config)) { goto failed; }
        diagnostic.stage = "interface-test";
        if (!controller_test(0xab, 0)) { goto failed; }
    }
    diagnostic.stage = "enable-port";
    if (!write_port(STATUS, 0xae)) { goto failed; }
    config &= (uint8_t)~0x10;
    /* Stop scanning before commands; explicitly select and verify native set 2. */
    diagnostic.stage = "stop-scanning";
    if (!keyboard_command(0xf5)) { goto failed; }
    diagnostic.stage = "select-set2";
    if (!keyboard_command(0xf0) || !keyboard_command(2)) { goto failed; }
    diagnostic.stage = "verify-set2";
    if (!keyboard_command(0xf0) || !keyboard_command(0) || !read_reply(&reply) || reply != 2) { goto failed; }
    diagnostic.stage = "LED-command";
    if (!keyboard_command(0xed) || !keyboard_command(0)) { goto failed; }
    diagnostic.stage = "config-readback";
    if (!config_write(config) || !write_port(STATUS, 0x20) || !read_reply(&reply) ||
        (reply & 0x73) != (config & 0x73)) { goto failed; }
    diagnostic.stage = "start-scanning";
    if (!keyboard_command(0xf4) || !config_write(config | 1)) { goto failed; }
    stats.ready = true;
    if (pic_unmask(1)) { diagnostic.stage = "ready"; return true; }
    diagnostic.stage = "unmask-IRQ1";
failed:
    stats.ready = false;
    pic_mask(1);
    const struct keyboard_diagnostics saved = diagnostic;
    (void)write_port(STATUS, 0xad);
    (void)flush();
    diagnostic = saved;
    return false;
}

bool keyboard_init_timed(void) {
    if (attempted || arch_irq_enabled() || !pic_is_ready()) { return false; }
    timed = true;
    return keyboard_init();
}
struct keyboard_diagnostics keyboard_get_diagnostics(void) { return diagnostic; }

static void input_lost(void) {
    stats.dropped += queued();
    tail = head;
    lost = true;
}

void keyboard_on_irq(void) {
    if (!stats.ready) { pic_mask(1); return; }
    ++stats.interrupts;
    for (unsigned int i = 0; i < IRQ_LIMIT; ++i) {
        const uint8_t status = inb(STATUS);
        if (!(status & 1)) { return; }
        const uint8_t scan = inb(DATA);
        if (status & 0x20) { continue; } /* Mouse port is disabled; never decode AUX data. */
        ++stats.received;
        if (status & 0xc0) {
            ++stats.errors;
            input_lost();
            ++stats.dropped;
            continue;
        }
        const unsigned int next = (head + 1) & (KEYBOARD_RX_CAPACITY - 1);
        if (next == tail) { input_lost(); ++stats.dropped; }
        else { buffer[head] = scan; head = next; }
    }
    if (!(inb(STATUS) & 1)) { return; }
    ++stats.errors;
    input_lost();
    stats.ready = false;
    stats.faulted = true;
    pic_mask(1);
}

enum keyboard_rx_result keyboard_read_key(uint8_t *value) {
    if (value == 0) { return KEYBOARD_RX_EMPTY; }
    const uint64_t flags = arch_irq_save();
    enum keyboard_rx_result result = KEYBOARD_RX_EMPTY;
    if (lost) {
        lost = false;
        decoder = (struct ps2_decoder){0};
        result = KEYBOARD_RX_LOST;
    } else {
        while (tail != head) {
            const uint8_t scan = buffer[tail];
            tail = (tail + 1) & (KEYBOARD_RX_CAPACITY - 1);
            const enum ps2_result decoded = ps2_decode(&decoder, scan, value);
            if (decoded == PS2_LOST) {
                ++stats.errors;
                stats.dropped += queued();
                tail = head;
                result = KEYBOARD_RX_LOST;
                break;
            }
            if (decoded == PS2_BYTE) { ++stats.decoded; result = KEYBOARD_RX_BYTE; break; }
        }
    }
    arch_irq_restore(flags);
    return result;
}

enum keyboard_rx_result keyboard_read(uint8_t *value) {
    enum keyboard_rx_result result;
    do { result = keyboard_read_key(value); }
    while (result == KEYBOARD_RX_BYTE && *value >= KEY_UP);
    return result;
}

struct keyboard_stats keyboard_get_stats(void) {
    const uint64_t flags = arch_irq_save();
    struct keyboard_stats result = stats;
    result.queued = queued();
    arch_irq_restore(flags);
    return result;
}
