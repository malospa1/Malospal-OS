#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malospal/arch.h>
#include <malospal/keyboard.h>
#include "../src/arch/x86_64/keyboard_irq.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Keyboard failure %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const char *mode;
static bool enabled, pic_ready, masked = true, config_pending, scanning, runtime, stuck;
static uint8_t config = 0x47, parameter, input[4096], status_flags;
static unsigned int begin, end, busy_cycles, reply_delay, polls, config_reads, resends;
static uint64_t ticks;
uint64_t timer_ticks(void) { return ticks; }
void arch_irq_wait(void) { CHECK(!enabled && masked); ++ticks; }
static bool is(const char *name) { return strcmp(mode, name) == 0; }
static void push(uint8_t value) { CHECK(end < sizeof(input)); input[end++] = value; }

bool arch_irq_enabled(void) { return enabled; }
uint64_t arch_irq_save(void) { const uint64_t old = enabled ? 512 : 0; enabled = false; return old; }
void arch_irq_restore(uint64_t flags) { enabled = (flags & 512) != 0; }
bool pic_is_ready(void) { return pic_ready; }
bool pic_mask(uint8_t irq) { CHECK(!enabled && irq == 1); masked = true; return true; }
bool pic_unmask(uint8_t irq) { CHECK(!enabled && irq == 1 && scanning && (config & 0x73) == 0x21); masked = false; return true; }

void outb(uint16_t port, uint8_t value) {
    CHECK(!enabled && !busy_cycles && (port == 0x60 || port == 0x64));
    busy_cycles = 2;
    if (port == 0x64) {
        switch (value) {
            case 0xad: config |= 0x10; break;
            case 0xae: config &= ~0x10; break;
            case 0xa7: config |= 0x20; break;
            case 0x20:
                push(config ^ ((is("config") && ++config_reads == 2) ? 0x40 : 0)); break;
            case 0x60: config_pending = true; break;
            case 0xaa: CHECK(!is("ec") && !is("slow-ec")); push(is("selftest") ? 0xfc : 0x55); config = 0x47; break;
            case 0xab: CHECK(!is("ec") && !is("slow-ec")); push(is("interface") ? 1 : 0); break;
            default: CHECK(false);
        }
    } else if (config_pending) {
        config = value; config_pending = false;
    } else {
        CHECK(!(config & 0x50)); /* Keyboard enabled, translation disabled. */
        if (value == 0xf5 && !parameter) {
            if (is("timeout") || is("ec-timeout")) { return; }
            if (is("ack")) { push(0); return; }
            if (is("resend") || resends++ < 2) { push(0xfe); return; }
            scanning = false;
        }
        push(0xfa);
        if (parameter) {
            if (parameter == 0xf0 && value == 0) { push(is("scanset") ? 1 : 2); }
            parameter = 0;
        } else if (value == 0xf0 || value == 0xed) { parameter = value; }
        else if (value == 0xf4) { scanning = true; }
        else { CHECK(value == 0xf5); }
    }
    reply_delay = is("slow-ec") ? 2000 : 3;
}

uint8_t inb(uint16_t port) {
    CHECK(!enabled);
    if (port == 0x64) {
        CHECK(++polls < 5000000);
        if (is("absent")) { return 0xff; }
        if (is("busy")) { return 2; }
        uint8_t result = 0;
        if (busy_cycles) { --busy_cycles; result |= 2; }
        if (reply_delay) { --reply_delay; }
        else if (begin < end || stuck) { result |= 1 | status_flags | (is("parity") && !runtime ? 0x80 : 0); }
        return result;
    }
    CHECK(port == 0x60);
    if (stuck) { return 0x1c; }
    CHECK(begin < end && !reply_delay);
    return input[begin++];
}

static void irq(void) {
    const uint64_t flags = arch_irq_save(); keyboard_on_irq(); arch_irq_restore(flags);
}

static void inject(unsigned int count, uint8_t scan) {
    CHECK(begin == end && count <= sizeof(input));
    begin = end = 0;
    for (unsigned int i = 0; i < count; ++i) { push(scan); }
    polls = reply_delay = 0;
    irq();
}

int main(int argc, char **argv) {
    CHECK(argc == 2); mode = argv[1];
    uint8_t value = 0;
    CHECK(!keyboard_init() && keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    pic_ready = true; enabled = true; CHECK(!keyboard_init()); enabled = false;
    if (!is("normal") && !is("ec") && !is("slow-ec")) {
        CHECK(!(is("ec-timeout") ? keyboard_init_timed() : keyboard_init()) && masked && !keyboard_get_stats().ready && !enabled);
        CHECK(strcmp(keyboard_get_diagnostics().stage, "not-started") != 0);
        if (is("ec-timeout")) { CHECK(ticks >= 50 && ticks < 100 && strcmp(keyboard_get_diagnostics().stage, "stop-scanning") == 0); }
        CHECK(!keyboard_init());
        printf("PASS: keyboard host %s failure is bounded, IRQ1 masked.\n", mode);
        return 0;
    }
    CHECK((is("normal") ? keyboard_init() : keyboard_init_timed()) && !masked && !enabled && !keyboard_init());
    CHECK(strcmp(keyboard_get_diagnostics().stage, "ready") == 0);
    if (is("slow-ec")) { CHECK(ticks > 0); }
    runtime = true; enabled = true;
    CHECK(keyboard_read(NULL) == KEYBOARD_RX_EMPTY && enabled);
    for (unsigned int round = 0; round < 4; ++round) {
        inject(200, 0x1c);
        for (unsigned int i = 0; i < 200; ++i) {
            CHECK(keyboard_read(&value) == KEYBOARD_RX_BYTE && value == 'a' && enabled);
        }
        CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY && keyboard_get_stats().ready);
    }
    inject(200, 0x1c); inject(100, 0x32);
    CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST && keyboard_get_stats().dropped == 256);
    for (unsigned int i = 0; i < 44; ++i) { CHECK(keyboard_read(&value) == KEYBOARD_RX_BYTE && value == 'b'); }
    CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    inject(1, 0x12); CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    status_flags = 0x80; inject(1, 0x1c); status_flags = 0;
    CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST);
    inject(1, 0x1c); CHECK(keyboard_read(&value) == KEYBOARD_RX_BYTE && value == 'a');
    status_flags = 0x40; inject(1, 0x1c); status_flags = 0;
    CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST);
    status_flags = 0x20; inject(2, 0x1c); status_flags = 0;
    CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    inject(1, 0xe0); CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    inject(1, 0xe0); CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST);
    inject(1, 0x1c); CHECK(keyboard_read(&value) == KEYBOARD_RX_BYTE && value == 'a');
    const struct keyboard_stats before = keyboard_get_stats();
    CHECK(before.errors == 3 && before.interrupts > 0 && before.decoded > 800 && !before.faulted && enabled);
    irq(); CHECK(!keyboard_get_stats().faulted);
    inject(256, 0x1c);
    CHECK(keyboard_get_stats().ready && !keyboard_get_stats().faulted);
    CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST);
    CHECK(keyboard_read(&value) == KEYBOARD_RX_EMPTY);
    stuck = true; polls = 0; irq();
    CHECK(keyboard_get_stats().faulted && !keyboard_get_stats().ready && masked && enabled);
    CHECK(keyboard_read(&value) == KEYBOARD_RX_LOST && !keyboard_init());
    puts("PASS: keyboard delayed I/O, resend, set 2, IRQ1, ring wrap/loss, AUX/errors, IF and stuck-device bound.");
    return 0;
}
