#include <malospal/arch.h>
#include <malospal/serial.h>
#include "io.h"
#include "pic.h"
#include "serial_irq.h"

enum { COM1 = 0x3f8, UART_POLL_LIMIT = 1000000, UART_RX_DRAIN_LIMIT = 4096 };
static bool available;
static bool receiving, lost;
static uint8_t rx_buffer[SERIAL_RX_CAPACITY];
static unsigned int rx_head, rx_tail;
static struct serial_stats stats;

_Static_assert((SERIAL_RX_CAPACITY & (SERIAL_RX_CAPACITY - 1)) == 0, "RX ring size");

static unsigned int queued(void) {
    return (rx_head - rx_tail) & (SERIAL_RX_CAPACITY - 1);
}

static void input_lost(void) {
    stats.dropped += queued();
    rx_tail = rx_head;
    lost = true;
}

/* IF must be clear. Every LSR read also accounts for receive errors, since
 * reading LSR for transmit readiness would otherwise clear them unnoticed. */
static uint8_t line_status(void) {
    const uint8_t status = inb(COM1 + 5);
    if (!receiving) {
        return status;
    }
    if (status & 0x1e) {
        ++stats.errors;
        input_lost();
    }
    if (status & 1) {
        const uint8_t value = inb(COM1);
        ++stats.received;
        const unsigned int next = (rx_head + 1) & (SERIAL_RX_CAPACITY - 1);
        if ((status & 0x1e) || next == rx_tail) {
            input_lost();
            ++stats.dropped;
        } else {
            rx_buffer[rx_head] = value;
            rx_head = next;
        }
    }
    return status;
}

bool serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x1e);
    outb(COM1 + 0, 0xae);
    available = false;
    /* Wait for both the loopback byte and an idle transmitter before leaving
     * loopback, so a pending transfer cannot consume the first output bytes. */
    for (unsigned int retry = 0; retry < UART_POLL_LIMIT; ++retry) {
        const uint8_t status = inb(COM1 + 5);
        if (status & 0x1e) {
            break;
        }
        if ((status & 0x41) == 0x41) {
            available = inb(COM1) == 0xae;
            break;
        }
        __asm__ volatile ("pause");
    }
    outb(COM1 + 4, 0x0b);
    outb(COM1 + 2, 0xc7);
    return available;
}

void serial_write_byte(char value) {
    if (!available) {
        return;
    }
    /* A missing or stalled UART must not trap the kernel in an endless wait. */
    for (unsigned int retry = 0; retry < UART_POLL_LIMIT; ++retry) {
        const uint64_t flags = arch_irq_save();
        const bool ready = (line_status() & 0x20) != 0;
        if (ready) {
            outb(COM1, (uint8_t)value);
        }
        arch_irq_restore(flags);
        if (ready) {
            return;
        }
        __asm__ volatile ("pause");
    }
}

bool serial_rx_init(void) {
    if (!available || receiving || stats.faulted || arch_irq_enabled() || !pic_is_ready()) {
        return false;
    }
    outb(COM1 + 2, 0x07); /* Clear FIFOs, receive trigger at one byte. */
    /* Some UARTs retain DR after a FIFO reset. Drain stale startup input before
     * enabling reception, so a leftover loopback byte cannot cancel a command. */
    unsigned int drained = 0;
    while (inb(COM1 + 5) & 1) {
        if (drained == UART_RX_DRAIN_LIMIT) {
            return false;
        }
        (void)inb(COM1);
        ++drained;
    }
    (void)inb(COM1 + 6);
    receiving = true;
    outb(COM1 + 1, 0x05); /* Receive data and line status; no transmit IRQ. */
    return pic_unmask(4);
}

void serial_on_irq(void) {
    if (!receiving) {
        pic_mask(4);
        return;
    }
    ++stats.interrupts;
    for (unsigned int retry = 0; retry < 4096; ++retry) {
        const uint8_t reason = inb(COM1 + 2);
        if (reason & 1) {
            return;
        }
        switch (reason & 0x0e) {
            case 0x04: /* Receive data. */
            case 0x0c: /* FIFO receive timeout. */
            case 0x06: /* Line status. */
                (void)line_status();
                break;
            default:
                goto fault;
        }
    }
fault:
    /* A stuck device must not monopolize the CPU with IF disabled. */
    outb(COM1 + 1, 0);
    pic_mask(4);
    receiving = false;
    stats.faulted = true;
    input_lost();
}

bool serial_rx_ready(void) { return receiving && !stats.faulted; }

enum serial_rx_result serial_read(uint8_t *value) {
    if (value == 0) {
        return SERIAL_RX_EMPTY;
    }
    const uint64_t flags = arch_irq_save();
    enum serial_rx_result result = SERIAL_RX_EMPTY;
    if (lost) {
        lost = false;
        result = SERIAL_RX_LOST;
    } else if (rx_head != rx_tail) {
        *value = rx_buffer[rx_tail];
        rx_tail = (rx_tail + 1) & (SERIAL_RX_CAPACITY - 1);
        result = SERIAL_RX_BYTE;
    }
    arch_irq_restore(flags);
    return result;
}

struct serial_stats serial_get_stats(void) {
    const uint64_t flags = arch_irq_save();
    struct serial_stats result = stats;
    result.queued = queued();
    arch_irq_restore(flags);
    return result;
}
