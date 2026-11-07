#ifndef MALOSPAL_X86_IO_H
#define MALOSPAL_X86_IO_H

#include <stdint.h>

#ifdef MALOSPAL_IO_TEST
void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
#else
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}
#endif

static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif
