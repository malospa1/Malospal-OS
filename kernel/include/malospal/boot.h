#ifndef MALOSPAL_BOOT_H
#define MALOSPAL_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define BOOT_MAX_MEMORY_REGIONS 512

enum boot_memory_type {
    BOOT_MEMORY_RESERVED,
    BOOT_MEMORY_USABLE,
    BOOT_MEMORY_ACPI_RECLAIMABLE,
    BOOT_MEMORY_ACPI_NVS,
    BOOT_MEMORY_BAD,
    BOOT_MEMORY_BOOTLOADER,
    BOOT_MEMORY_KERNEL_MODULES,
    BOOT_MEMORY_FRAMEBUFFER,
};

struct boot_memory_region {
    uint64_t base;
    uint64_t length;
    enum boot_memory_type type;
};

struct boot_framebuffer {
    uint64_t address, physical_address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};

struct boot_info {
    uint64_t memory_region_count;
    uint64_t usable_memory_bytes;
    uint64_t direct_map_offset;
    uint64_t kernel_physical_base;
    uint64_t kernel_virtual_base;
    struct boot_framebuffer framebuffer;
    struct boot_memory_region regions[BOOT_MAX_MEMORY_REGIONS];
};

bool boot_read_info(struct boot_info *info);
bool framebuffer_format_valid(const struct boot_framebuffer *fb);
bool boot_framebuffer_valid(const struct boot_info *info);

#endif
