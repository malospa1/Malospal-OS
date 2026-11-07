#include <malospal/pmm.h>

struct page_range {
    uint64_t base;
    uint64_t pages;
    uint64_t first_bit;
};

static struct page_range ranges[BOOT_MAX_MEMORY_REGIONS];
static uint64_t range_count;
static unsigned char *bitmap;
static uint64_t next_bit;
static struct pmm_stats stats;
static bool ready;

static bool bit_is_set(uint64_t bit) {
    return (bitmap[bit / 8] & (1u << (bit % 8))) != 0;
}

static void set_bit(uint64_t bit, bool allocated) {
    const unsigned char mask = (unsigned char)(1u << (bit % 8));
    if (allocated) {
        bitmap[bit / 8] |= mask;
    } else {
        bitmap[bit / 8] &= (unsigned char)~mask;
    }
}

static bool valid_map(const struct boot_info *info) {
    if (info == 0 || info->memory_region_count == 0 ||
        info->memory_region_count > BOOT_MAX_MEMORY_REGIONS ||
        info->direct_map_offset % PMM_PAGE_SIZE != 0) {
        return false;
    }
    for (uint64_t i = 0; i < info->memory_region_count; ++i) {
        const struct boot_memory_region *a = &info->regions[i];
        if (a->length > UINT64_MAX - a->base ||
            (i != 0 && a->base < info->regions[i - 1].base)) {
            return false;
        }
    }
    /* Non-usable entries may overlap each other, but never usable memory. */
    for (uint64_t i = 0; i < info->memory_region_count; ++i) {
        const struct boot_memory_region *a = &info->regions[i];
        if (a->type != BOOT_MEMORY_USABLE || a->length == 0) {
            continue;
        }
        for (uint64_t j = 0; j < info->memory_region_count; ++j) {
            const struct boot_memory_region *b = &info->regions[j];
            if (i != j && b->length != 0 && a->base < b->base + b->length &&
                b->base < a->base + a->length) {
                return false;
            }
        }
    }
    return true;
}

bool pmm_init(const struct boot_info *info) {
    if (ready || !valid_map(info)) {
        return false;
    }
    range_count = 0;
    stats = (struct pmm_stats){0};
    for (uint64_t i = 0; i < info->memory_region_count; ++i) {
        const struct boot_memory_region *region = &info->regions[i];
        if (region->type != BOOT_MEMORY_USABLE) {
            continue;
        }
        uint64_t first = region->base / PMM_PAGE_SIZE + (region->base % PMM_PAGE_SIZE != 0);
        const uint64_t end = (region->base + region->length) / PMM_PAGE_SIZE;
        if (first == 0) {
            first = 1;
        }
        if (first >= end) {
            continue;
        }
        if (end * PMM_PAGE_SIZE - 1 > UINTPTR_MAX - info->direct_map_offset) {
            return false;
        }
        ranges[range_count++] = (struct page_range){
            first * PMM_PAGE_SIZE, end - first, stats.total_pages,
        };
        stats.total_pages += end - first;
    }

    /* Dense bits for usable ranges avoid allocating a bitmap for address holes. */
    const uint64_t bytes = (stats.total_pages + 7) / 8;
    stats.metadata_pages = (bytes + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (stats.total_pages <= stats.metadata_pages) {
        return false;
    }
    const struct page_range *storage = 0;
    for (uint64_t i = 0; i < range_count; ++i) {
        if (ranges[i].pages >= stats.metadata_pages) {
            storage = &ranges[i];
            break;
        }
    }
    if (storage == 0 || storage->base + info->direct_map_offset == 0) {
        return false;
    }
    stats.metadata_base = storage->base;
    bitmap = (unsigned char *)(uintptr_t)(storage->base + info->direct_map_offset);
    for (uint64_t i = 0; i < stats.metadata_pages * PMM_PAGE_SIZE; ++i) {
        bitmap[i] = 0;
    }
    for (uint64_t i = 0; i < stats.metadata_pages; ++i) {
        set_bit(storage->first_bit + i, true);
    }
    stats.free_pages = stats.total_pages - stats.metadata_pages;
    next_bit = 0;
    ready = true;
    return true;
}

bool pmm_alloc_page(uint64_t *physical_address) {
    if (!ready || physical_address == 0 || stats.free_pages == 0) {
        return false;
    }
    uint64_t bit = next_bit;
    for (uint64_t scanned = 0; scanned < stats.total_pages; ++scanned) {
        if (!bit_is_set(bit)) {
            for (uint64_t i = 0; i < range_count; ++i) {
                const struct page_range *range = &ranges[i];
                if (bit >= range->first_bit && bit - range->first_bit < range->pages) {
                    *physical_address = range->base + (bit - range->first_bit) * PMM_PAGE_SIZE;
                    set_bit(bit, true);
                    --stats.free_pages;
                    ++stats.allocated_pages;
                    next_bit = bit + 1 == stats.total_pages ? 0 : bit + 1;
                    return true;
                }
            }
            return false;
        }
        bit = bit + 1 == stats.total_pages ? 0 : bit + 1;
    }
    return false;
}

bool pmm_free_page(uint64_t physical_address) {
    if (!ready || physical_address == 0 || physical_address % PMM_PAGE_SIZE != 0 ||
        (physical_address >= stats.metadata_base &&
         physical_address - stats.metadata_base < stats.metadata_pages * PMM_PAGE_SIZE)) {
        return false;
    }
    for (uint64_t i = 0; i < range_count; ++i) {
        const struct page_range *range = &ranges[i];
        if (physical_address < range->base ||
            (physical_address - range->base) / PMM_PAGE_SIZE >= range->pages) {
            continue;
        }
        const uint64_t bit = range->first_bit + (physical_address - range->base) / PMM_PAGE_SIZE;
        if (!bit_is_set(bit)) {
            return false;
        }
        set_bit(bit, false);
        ++stats.free_pages;
        --stats.allocated_pages;
        next_bit = bit;
        return true;
    }
    return false;
}

struct pmm_stats pmm_get_stats(void) {
    return ready ? stats : (struct pmm_stats){0};
}
