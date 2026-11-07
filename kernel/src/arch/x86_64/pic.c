#include "pic.h"
#include "io.h"

#define MASTER_COMMAND 0x20
#define MASTER_DATA 0x21
#define SLAVE_COMMAND 0xa0
#define SLAVE_DATA 0xa1
#define PIC_EOI 0x20

static bool ready;
static uint8_t master_mask = 0xff;
static uint8_t slave_mask = 0xff;

static void write_delayed(uint16_t port, uint8_t value) {
    outb(port, value);
    io_wait();
}

bool pic_init(void) {
    if (ready) {
        return false;
    }
    write_delayed(MASTER_DATA, 0xff);
    write_delayed(SLAVE_DATA, 0xff);
    write_delayed(MASTER_COMMAND, 0x11);
    write_delayed(SLAVE_COMMAND, 0x11);
    write_delayed(MASTER_DATA, PIC_VECTOR_BASE);
    write_delayed(SLAVE_DATA, PIC_VECTOR_BASE + 8);
    write_delayed(MASTER_DATA, 1u << 2);
    write_delayed(SLAVE_DATA, 2);
    write_delayed(MASTER_DATA, 1);
    write_delayed(SLAVE_DATA, 1);
    write_delayed(MASTER_DATA, master_mask);
    write_delayed(SLAVE_DATA, slave_mask);
    ready = true;
    return true;
}

bool pic_is_ready(void) {
    return ready;
}

bool pic_mask(uint8_t irq) {
    if (!ready || irq >= PIC_IRQ_COUNT || irq == 2) {
        return false;
    }
    if (irq < 8) {
        master_mask |= (uint8_t)(1u << irq);
        outb(MASTER_DATA, master_mask);
    } else {
        slave_mask |= (uint8_t)(1u << (irq - 8));
        outb(SLAVE_DATA, slave_mask);
        if (slave_mask == 0xff) {
            master_mask |= 1u << 2;
            outb(MASTER_DATA, master_mask);
        }
    }
    return true;
}

bool pic_unmask(uint8_t irq) {
    if (!ready || irq >= PIC_IRQ_COUNT || irq == 2) {
        return false;
    }
    if (irq < 8) {
        master_mask &= (uint8_t)~(1u << irq);
        outb(MASTER_DATA, master_mask);
    } else {
        slave_mask &= (uint8_t)~(1u << (irq - 8));
        outb(SLAVE_DATA, slave_mask);
        master_mask &= (uint8_t)~(1u << 2);
        outb(MASTER_DATA, master_mask);
    }
    return true;
}

bool pic_begin_irq(uint8_t irq) {
    if (!ready || irq >= PIC_IRQ_COUNT) {
        return false;
    }
    if (irq == 7 || irq == 15) {
        const uint16_t command = irq == 7 ? MASTER_COMMAND : SLAVE_COMMAND;
        outb(command, 0x0b);
        if ((inb(command) & 0x80) == 0) {
            if (irq == 15) {
                outb(MASTER_COMMAND, PIC_EOI);
            }
            return false;
        }
    }
    return true;
}

void pic_end_irq(uint8_t irq) {
    if (!ready || irq >= PIC_IRQ_COUNT) {
        return;
    }
    if (irq >= 8) {
        outb(SLAVE_COMMAND, PIC_EOI);
    }
    outb(MASTER_COMMAND, PIC_EOI);
}
