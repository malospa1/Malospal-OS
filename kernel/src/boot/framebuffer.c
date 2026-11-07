#include <malospal/boot.h>

bool framebuffer_format_valid(const struct boot_framebuffer *fb) {
    if (fb == 0 || fb->address == 0 || fb->width < 48 || fb->height < 48 ||
        fb->width > 4096 || fb->height > 2160 || (fb->bpp != 24 && fb->bpp != 32) ||
        fb->pitch < fb->width * (fb->bpp / 8) ||
        fb->pitch > UINT64_C(64) * 1024 * 1024 / fb->height ||
        fb->pitch * fb->height > UINT64_MAX - fb->address ||
        fb->red_size != 8 || fb->green_size != 8 || fb->blue_size != 8 ||
        fb->red_shift > fb->bpp - 8 || fb->green_shift > fb->bpp - 8 ||
        fb->blue_shift > fb->bpp - 8) {
        return false;
    }
    const uint32_t red = 255u << fb->red_shift, green = 255u << fb->green_shift;
    const uint32_t blue = 255u << fb->blue_shift;
    return !(red & green) && !(red & blue) && !(green & blue);
}

bool boot_framebuffer_valid(const struct boot_info *info) {
    if (info == 0 || info->memory_region_count > BOOT_MAX_MEMORY_REGIONS ||
        !framebuffer_format_valid(&info->framebuffer)) {
        return false;
    }
    const struct boot_framebuffer *fb = &info->framebuffer;
    const uint64_t size = fb->pitch * fb->height;
    if (info->direct_map_offset < UINT64_C(0xffff800000000000) ||
        info->direct_map_offset % 4096 != 0 || fb->physical_address == 0 ||
        fb->physical_address > UINT64_MAX - info->direct_map_offset ||
        fb->address != info->direct_map_offset + fb->physical_address ||
        size > UINT64_MAX - fb->physical_address - 4095) {
        return false;
    }
    const uint64_t first = fb->physical_address & ~UINT64_C(4095);
    const uint64_t end = (fb->physical_address + size + 4095) & ~UINT64_C(4095);
    bool covered = false;
    for (uint64_t i = 0; i < info->memory_region_count; ++i) {
        const struct boot_memory_region *r = &info->regions[i];
        if (r->length > UINT64_MAX - r->base) { return false; }
        const uint64_t stop = r->base + r->length;
        if (r->length && r->base < end && stop > first) {
            if (r->type != BOOT_MEMORY_FRAMEBUFFER) { return false; }
            if (r->base <= first && stop >= end) { covered = true; }
        }
    }
    return covered;
}
