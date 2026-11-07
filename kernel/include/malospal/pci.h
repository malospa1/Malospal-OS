#ifndef MALOSPAL_PCI_H
#define MALOSPAL_PCI_H
#include <stdint.h>
/* Segment zero, configuration mechanism 1. Firmware-assigned buses and BARs. */
uint32_t pci_read(uint16_t bdf, uint8_t offset);
void pci_write(uint16_t bdf, uint8_t offset, uint32_t value);
void pci_command(uint16_t bdf, uint16_t value);
#endif
