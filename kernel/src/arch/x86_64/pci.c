#include <malospal/arch.h>
#include <malospal/pci.h>

static void out32(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

uint32_t pci_read(uint16_t bdf, uint8_t offset) {
    const uint64_t flags = arch_irq_save();
    out32(0xcf8, 0x80000000u | (uint32_t)bdf << 8 | (offset & 0xfc));
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"((uint16_t)0xcfc) : "memory");
    arch_irq_restore(flags);
    return value;
}

void pci_write(uint16_t bdf, uint8_t offset, uint32_t value) {
    const uint64_t flags = arch_irq_save();
    out32(0xcf8, 0x80000000u | (uint32_t)bdf << 8 | (offset & 0xfc));
    out32(0xcfc, value);
    arch_irq_restore(flags);
}

void pci_command(uint16_t bdf, uint16_t value) {
    /* Writing zero to status avoids acknowledging unrelated W1C status bits. */
    pci_write(bdf, 4, value);
}
