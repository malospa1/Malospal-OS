#include <limine.h>
#include <malospal/boot.h>

__attribute__((used, section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request direct_map_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0,
};

__attribute__((used, section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER

static enum boot_memory_type memory_type(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE: return BOOT_MEMORY_USABLE;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return BOOT_MEMORY_ACPI_RECLAIMABLE;
        case LIMINE_MEMMAP_ACPI_NVS: return BOOT_MEMORY_ACPI_NVS;
        case LIMINE_MEMMAP_BAD_MEMORY: return BOOT_MEMORY_BAD;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return BOOT_MEMORY_BOOTLOADER;
        case LIMINE_MEMMAP_KERNEL_AND_MODULES: return BOOT_MEMORY_KERNEL_MODULES;
        case LIMINE_MEMMAP_FRAMEBUFFER: return BOOT_MEMORY_FRAMEBUFFER;
        default: return BOOT_MEMORY_RESERVED;
    }
}

bool boot_read_info(struct boot_info *info) {
    if (info == 0 || !LIMINE_BASE_REVISION_SUPPORTED || memory_request.response == 0 ||
        direct_map_request.response == 0 || address_request.response == 0) {
        return false;
    }
    const struct limine_memmap_response *map = memory_request.response;
    if (map->entry_count == 0 || map->entry_count > BOOT_MAX_MEMORY_REGIONS || map->entries == 0) {
        return false;
    }
    info->memory_region_count = map->entry_count;
    info->usable_memory_bytes = 0;
    info->direct_map_offset = direct_map_request.response->offset;
    info->kernel_physical_base = address_request.response->physical_base;
    info->kernel_virtual_base = address_request.response->virtual_base;
    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *region = map->entries[i];
        if (region == 0 || region->length > UINT64_MAX - region->base) {
            return false;
        }
        info->regions[i] = (struct boot_memory_region){
            region->base, region->length, memory_type(region->type),
        };
        if (region->type == LIMINE_MEMMAP_USABLE) {
            if (region->length > UINT64_MAX - info->usable_memory_bytes) {
                return false;
            }
            info->usable_memory_bytes += region->length;
        }
    }
    info->framebuffer = (struct boot_framebuffer){0};
    const struct limine_framebuffer_response *video = framebuffer_request.response;
    if (video != 0 && video->framebuffers != 0) {
        for (uint64_t i = 0; i < video->framebuffer_count && i < 16; ++i) {
            const struct limine_framebuffer *fb = video->framebuffers[i];
            if (fb == 0 || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
                (uintptr_t)fb->address < info->direct_map_offset) { continue; }
            info->framebuffer = (struct boot_framebuffer){
                .address = (uintptr_t)fb->address,
                .physical_address = (uintptr_t)fb->address - info->direct_map_offset,
                .width = fb->width, .height = fb->height, .pitch = fb->pitch, .bpp = fb->bpp,
                .red_size = fb->red_mask_size, .red_shift = fb->red_mask_shift,
                .green_size = fb->green_mask_size, .green_shift = fb->green_mask_shift,
                .blue_size = fb->blue_mask_size, .blue_shift = fb->blue_mask_shift,
            };
            if (boot_framebuffer_valid(info)) { break; }
            info->framebuffer = (struct boot_framebuffer){0};
        }
    }
    return info->usable_memory_bytes != 0;
}
