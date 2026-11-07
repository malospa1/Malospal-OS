#include <malospal/layout.h>
#include <malospal/pmm.h>
#include <malospal/vmm.h>

#define PRESENT UINT64_C(1)
#define WRITE UINT64_C(2)
#define LARGE UINT64_C(0x80)
#define NX (UINT64_C(1) << 63)
#define ADDRESS UINT64_C(0x000ffffffffff000)
#define PWT UINT64_C(8)
#define PCD UINT64_C(16)
#define PAT UINT64_C(0x80)
#define FRAMEBUFFER 4u
#define DEVICE 8u

static uint64_t root, direct_map, physical_limit, table_pages, framebuffer_cache;
static bool active;
static struct boot_info memory_layout;

static bool canonical(uint64_t address) {
    return address <= UINT64_C(0x00007fffffffffff) || address >= UINT64_C(0xffff800000000000);
}

static bool dynamic_page(uint64_t address) {
    return address >= VMM_WINDOW_BASE && address - VMM_WINDOW_BASE < VMM_WINDOW_SIZE &&
           address % PMM_PAGE_SIZE == 0;
}

static bool device_page(uint64_t address) {
    return address >= VMM_MMIO_BASE && address - VMM_MMIO_BASE < VMM_MMIO_SIZE &&
           address % PMM_PAGE_SIZE == 0;
}

static uint64_t *table(uint64_t physical) {
    return (uint64_t *)(uintptr_t)(direct_map + physical);
}

static unsigned int index_at(uint64_t address, unsigned int level) {
    return (unsigned int)((address >> (12 + 9 * (level - 1))) & 511);
}

static void invalidate(uint64_t address) {
    if (active) {
        __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
    }
}

static bool allocate_table(uint64_t *physical) {
    if (!pmm_alloc_page(physical)) {
        return false;
    }
    if (*physical > physical_limit - (PMM_PAGE_SIZE - 1)) {
        pmm_free_page(*physical);
        return false;
    }
    uint64_t *entries = table(*physical);
    for (unsigned int i = 0; i < 512; ++i) {
        entries[i] = 0;
    }
    ++table_pages;
    return true;
}

static void free_table(uint64_t physical) {
    pmm_free_page(physical);
    --table_pages;
}

static uint64_t leaf_flags(unsigned int flags) {
    return PRESENT | ((flags & VMM_WRITE) ? WRITE : 0) | ((flags & VMM_EXECUTE) ? 0 : NX) |
           ((flags & FRAMEBUFFER) ? framebuffer_cache : 0) |
           ((flags & DEVICE) ? PWT | PCD : 0);
}

static bool map_page(uint64_t virtual, uint64_t physical, unsigned int flags) {
    uint64_t *entries = table(root);
    uint64_t *new_slots[3];
    uint64_t new_pages[3];
    unsigned int created = 0;
    for (unsigned int level = 4; level > 1; --level) {
        uint64_t *slot = &entries[index_at(virtual, level)];
        if (!(*slot & PRESENT)) {
            uint64_t child;
            if (!allocate_table(&child)) {
                goto rollback;
            }
            new_slots[created] = slot;
            new_pages[created++] = child;
            *slot = child | PRESENT | WRITE;
        } else if (*slot & LARGE) {
            goto rollback;
        }
        entries = table(*slot & ADDRESS);
    }
    uint64_t *leaf = &entries[index_at(virtual, 1)];
    if (*leaf & PRESENT) {
        goto rollback;
    }
    *leaf = physical | leaf_flags(flags);
    invalidate(virtual);
    return true;

rollback:
    /* Only empty branches created by this call may be removed. */
    while (created != 0) {
        --created;
        *new_slots[created] = 0;
        invalidate(virtual);
        free_table(new_pages[created]);
    }
    return false;
}

static uint64_t *find_leaf(uint64_t virtual) {
    uint64_t *entries = table(root);
    for (unsigned int level = 4; level > 1; --level) {
        const uint64_t entry = entries[index_at(virtual, level)];
        if (!(entry & PRESENT) || (entry & LARGE)) {
            return 0;
        }
        entries = table(entry & ADDRESS);
    }
    return &entries[index_at(virtual, 1)];
}

static void destroy_tables(uint64_t physical, unsigned int level) {
    uint64_t *entries = table(physical);
    if (level > 1) {
        for (unsigned int i = 0; i < 512; ++i) {
            if (entries[i] & PRESENT) {
                destroy_tables(entries[i] & ADDRESS, level - 1);
            }
        }
    }
    free_table(physical);
}

static bool cpu_supported(void) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    framebuffer_cache = PWT | PCD;
    if (d & (1u << 16)) {
        uint32_t low, high;
        __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));
        /* Limine specifies PAT5=WC and PAT3=UC. Preserve its WC mapping. */
        if ((low >> 24) != 0) { return false; }
        if (((high >> 8) & 255) == 1) { framebuffer_cache = PWT | PAT; }
    }
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a < 0x80000008u) {
        return false;
    }
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001u), "c"(0));
    if (!(d & (1u << 20))) {
        return false;
    }
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000008u), "c"(0));
    const unsigned int bits = a & 255;
    uint64_t cr4;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    if (bits < 32 || bits > 52 || (cr4 & ((1u << 12) | (1u << 17)))) {
        return false;
    }
    physical_limit = (UINT64_C(1) << bits) - 1;
    return true;
}

static void activate(void) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080u));
    low |= 1u << 11;
    __asm__ volatile ("wrmsr" : : "a"(low), "d"(high), "c"(0xc0000080u) : "memory");
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= UINT64_C(1) << 16;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    /* Drop any global translations inherited from the bootloader as well. */
    const uint64_t no_global = cr4 & ~(UINT64_C(1) << 7);
    __asm__ volatile ("mov %0, %%cr4" : : "r"(no_global) : "memory");
    __asm__ volatile ("mov %0, %%cr3" : : "r"(root) : "memory");
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

static bool guard_page(uint64_t address) {
    const unsigned char *guards[] = {
        kernel_stack_guard_low, kernel_stack_guard_high,
        double_fault_stack_guard_low, double_fault_stack_guard_high,
        nmi_stack_guard_low, nmi_stack_guard_high,
        machine_check_stack_guard_low, machine_check_stack_guard_high,
    };
    for (unsigned int i = 0; i < sizeof(guards) / sizeof(guards[0]); ++i) {
        if (address == (uintptr_t)guards[i]) {
            return true;
        }
    }
    return false;
}

bool vmm_init(const struct boot_info *boot) {
    if (active || root != 0 || boot == 0 || !cpu_supported() ||
        pmm_get_stats().total_pages == 0 || boot->memory_region_count == 0 ||
        boot->memory_region_count > BOOT_MAX_MEMORY_REGIONS ||
        boot->kernel_virtual_base != (uintptr_t)kernel_image_start ||
        boot->kernel_physical_base % PMM_PAGE_SIZE != 0 ||
        boot->direct_map_offset % PMM_PAGE_SIZE != 0 ||
        boot->direct_map_offset < UINT64_C(0xffff800000000000)) {
        return false;
    }
    direct_map = boot->direct_map_offset;
    const uint64_t image_size = (uintptr_t)kernel_image_end - (uintptr_t)kernel_image_start;
    if (boot->kernel_physical_base > physical_limit || image_size == 0 ||
        image_size - 1 > physical_limit - boot->kernel_physical_base || !allocate_table(&root)) {
        root = 0;
        return false;
    }
    for (uint64_t virtual = (uintptr_t)kernel_image_start; virtual < (uintptr_t)kernel_image_end;
         virtual += PMM_PAGE_SIZE) {
        if (guard_page(virtual)) {
            continue;
        }
        unsigned int flags = VMM_WRITE;
        if (virtual >= (uintptr_t)kernel_text_start && virtual < (uintptr_t)kernel_rodata_start) {
            flags = VMM_EXECUTE;
        } else if (virtual >= (uintptr_t)kernel_rodata_start && virtual < (uintptr_t)kernel_data_start) {
            flags = 0;
        }
        if (!map_page(virtual, boot->kernel_physical_base + virtual - boot->kernel_virtual_base, flags)) {
            goto failed;
        }
    }
    for (uint64_t i = 0; i < boot->memory_region_count; ++i) {
        const struct boot_memory_region *r = &boot->regions[i];
        if (r->type != BOOT_MEMORY_USABLE || r->length == 0) {
            continue;
        }
        if (r->length > UINT64_MAX - r->base) {
            goto failed;
        }
        uint64_t first = r->base / PMM_PAGE_SIZE + (r->base % PMM_PAGE_SIZE != 0);
        const uint64_t end = (r->base + r->length) / PMM_PAGE_SIZE;
        if (first == 0) { first = 1; }
        for (uint64_t p = first; p < end; ++p) {
            const uint64_t physical = p * PMM_PAGE_SIZE;
            if (physical > physical_limit - (PMM_PAGE_SIZE - 1) ||
                physical > UINT64_MAX - direct_map - (PMM_PAGE_SIZE - 1) ||
                (physical >= boot->kernel_physical_base && physical - boot->kernel_physical_base < image_size)) {
                goto failed;
            }
            const uint64_t virtual = direct_map + physical;
            if (!canonical(virtual) || dynamic_page(virtual) || device_page(virtual) ||
                (virtual >= (uintptr_t)kernel_image_start && virtual < (uintptr_t)kernel_image_end) ||
                !map_page(virtual, physical, VMM_WRITE)) {
                goto failed;
            }
        }
    }
    if (boot->framebuffer.address != 0) {
        if (!boot_framebuffer_valid(boot)) { goto failed; }
        const struct boot_framebuffer *fb = &boot->framebuffer;
        const uint64_t first = fb->physical_address & ~(PMM_PAGE_SIZE - 1);
        const uint64_t end = (fb->physical_address + fb->pitch * fb->height + PMM_PAGE_SIZE - 1) &
                             ~(PMM_PAGE_SIZE - 1);
        for (uint64_t physical = first; physical < end; physical += PMM_PAGE_SIZE) {
            if (physical > physical_limit - (PMM_PAGE_SIZE - 1) ||
                physical > UINT64_MAX - direct_map - (PMM_PAGE_SIZE - 1)) { goto failed; }
            const uint64_t virtual = direct_map + physical;
            if (!canonical(virtual) || dynamic_page(virtual) || device_page(virtual) ||
                (virtual >= (uintptr_t)kernel_image_start && virtual < (uintptr_t)kernel_image_end) ||
                !map_page(virtual, physical, VMM_WRITE | FRAMEBUFFER)) { goto failed; }
        }
    }
    __asm__ volatile ("sfence" : : : "memory");
    activate();
    active = true;
    memory_layout = *boot;
    return true;

failed:
    destroy_tables(root, 4);
    root = 0;
    return false;
}

bool vmm_map_page(uint64_t virtual, uint64_t physical, unsigned int flags) {
    if (!active || !dynamic_page(virtual) || flags & ~VMM_WRITE ||
        physical == 0 || physical % PMM_PAGE_SIZE != 0 || physical > physical_limit - (PMM_PAGE_SIZE - 1)) {
        return false;
    }
    /* Only direct-mapped RAM may be aliased; this excludes kernel text and guards. */
    struct vmm_mapping mapping;
    if (physical > UINT64_MAX - direct_map || !vmm_query(direct_map + physical, &mapping) ||
        mapping.physical_address != physical) {
        return false;
    }
    if (*find_leaf(direct_map + physical) & (PWT | PCD | PAT)) { return false; }
    return map_page(virtual, physical, flags);
}

bool vmm_protect_page(uint64_t virtual, unsigned int flags) {
    if (!active || !dynamic_page(virtual) || flags & ~VMM_WRITE) {
        return false;
    }
    uint64_t *leaf = find_leaf(virtual);
    if (leaf == 0 || !(*leaf & PRESENT)) {
        return false;
    }
    *leaf = (*leaf & ADDRESS) | leaf_flags(flags);
    invalidate(virtual);
    return true;
}

static bool unmap_page(uint64_t virtual, uint64_t *physical) {
    if (!active || physical == 0) {
        return false;
    }
    uint64_t pages[4] = {root};
    uint64_t *slots[3];
    uint64_t *entries = table(root);
    for (unsigned int depth = 0; depth < 3; ++depth) {
        slots[depth] = &entries[index_at(virtual, 4 - depth)];
        if (!(*slots[depth] & PRESENT) || (*slots[depth] & LARGE)) {
            return false;
        }
        pages[depth + 1] = *slots[depth] & ADDRESS;
        entries = table(pages[depth + 1]);
    }
    uint64_t *leaf = &entries[index_at(virtual, 1)];
    if (!(*leaf & PRESENT)) { return false; }
    *physical = *leaf & ADDRESS;
    *leaf = 0;
    invalidate(virtual);
    for (unsigned int depth = 3; depth > 0; --depth) {
        entries = table(pages[depth]);
        for (unsigned int i = 0; i < 512; ++i) {
            if (entries[i] & PRESENT) { return true; }
        }
        *slots[depth - 1] = 0;
        invalidate(virtual);
        free_table(pages[depth]);
    }
    return true;
}

bool vmm_unmap_page(uint64_t virtual, uint64_t *physical) {
    return dynamic_page(virtual) && unmap_page(virtual, physical);
}

bool vmm_unmap_mmio_page(uint64_t virtual) {
    uint64_t physical;
    return device_page(virtual) && unmap_page(virtual, &physical);
}

bool vmm_map_mmio_page(uint64_t virtual, uint64_t physical) {
    if (!active || !device_page(virtual) || physical < 0x100000 ||
        physical % PMM_PAGE_SIZE || physical > physical_limit - (PMM_PAGE_SIZE - 1)) { return false; }
    const uint64_t end = physical + PMM_PAGE_SIZE;
    const uint64_t image_size = (uintptr_t)kernel_image_end - (uintptr_t)kernel_image_start;
    if (physical < memory_layout.kernel_physical_base + image_size &&
        end > memory_layout.kernel_physical_base) { return false; }
    for (uint64_t i = 0; i < memory_layout.memory_region_count; ++i) {
        const struct boot_memory_region *r = &memory_layout.regions[i];
        if (r->length > UINT64_MAX - r->base) { return false; }
        if (r->type != BOOT_MEMORY_RESERVED && physical < r->base + r->length &&
            end > r->base) { return false; }
    }
    const struct boot_framebuffer *fb = &memory_layout.framebuffer;
    if (fb->address && physical < fb->physical_address + fb->pitch * fb->height &&
        end > fb->physical_address) { return false; }
    return map_page(virtual, physical, VMM_WRITE | DEVICE);
}

bool vmm_query(uint64_t virtual, struct vmm_mapping *mapping) {
    if (!active || !canonical(virtual) || mapping == 0) { return false; }
    const uint64_t *leaf = find_leaf(virtual);
    if (leaf == 0 || !(*leaf & PRESENT)) { return false; }
    mapping->physical_address = (*leaf & ADDRESS) | (virtual & (PMM_PAGE_SIZE - 1));
    mapping->flags = ((*leaf & WRITE) ? VMM_WRITE : 0) | ((*leaf & NX) ? 0 : VMM_EXECUTE);
    return true;
}

uint64_t vmm_root_physical(void) { return active ? root : 0; }
uint64_t vmm_table_pages(void) { return active ? table_pages : 0; }
