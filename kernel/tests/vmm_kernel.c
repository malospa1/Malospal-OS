#include <malospal/debug.h>
#include <malospal/layout.h>
#include <malospal/pmm.h>
#include <malospal/vmm.h>

#define REQUIRE(test) do { \
    if (!(test)) { \
        debug_write("[test] VMM failure at line "); \
        debug_u64(__LINE__); \
        debug_write(": " #test "\n"); \
        return false; \
    } \
} while (0)

bool vmm_test_initialization(const struct boot_info *boot) {
    const struct pmm_stats initial = pmm_get_stats();
    uint64_t original_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(original_cr3));
    REQUIRE(initial.free_pages > 16 && initial.allocated_pages == 0);
    struct vmm_mapping mapping;
    uint64_t removed;
    REQUIRE(!vmm_query((uintptr_t)kernel_text_start, &mapping));
    REQUIRE(!vmm_map_page(VMM_WINDOW_BASE, PMM_PAGE_SIZE, 0));
    REQUIRE(!vmm_unmap_page(VMM_WINDOW_BASE, &removed));
    REQUIRE(!vmm_protect_page(VMM_WINDOW_BASE, 0));
    REQUIRE(!vmm_init(0));
    static struct boot_info invalid;
    invalid = *boot;
    invalid.memory_region_count = 0;
    REQUIRE(!vmm_init(&invalid));
    invalid.memory_region_count = BOOT_MAX_MEMORY_REGIONS + 1;
    REQUIRE(!vmm_init(&invalid));

    uint64_t head = 0, page, count = 0;
    while (pmm_alloc_page(&page)) {
        REQUIRE(count < initial.free_pages);
        *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page) = head;
        head = page;
        ++count;
    }
    REQUIRE(count == initial.free_pages);
    /* Fail before root allocation and after partial kernel/direct-map setup. */
    for (unsigned int available = 0; available <= 6; ++available) {
        if (available != 0) {
            REQUIRE(head != 0);
            page = head;
            head = *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
            REQUIRE(pmm_free_page(page));
            --count;
        }
        REQUIRE(!vmm_init(boot));
        uint64_t current_cr3;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
        REQUIRE(current_cr3 == original_cr3 && vmm_root_physical() == 0);
        REQUIRE(pmm_get_stats().free_pages == available);
        REQUIRE(pmm_get_stats().allocated_pages == initial.free_pages - available);
    }
    while (head != 0) {
        REQUIRE(count != 0);
        page = head;
        head = *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
        REQUIRE(pmm_free_page(page));
        --count;
    }
    REQUIRE(count == 0 && pmm_get_stats().free_pages == initial.free_pages);
    REQUIRE(pmm_get_stats().allocated_pages == 0);
    debug_write("[test] VMM initialization failure keeps old CR3 and releases tables: OK\n");
    return true;
}

static bool check_layout(const struct boot_info *boot) {
    struct vmm_mapping m;
    uint64_t cr3, cr0;
    uint32_t low, high;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000080u));
    REQUIRE(cr3 == vmm_root_physical() && (cr0 & (1u << 16)) && (low & (1u << 11)));
    REQUIRE(!vmm_query(0, &m));
    REQUIRE(vmm_query((uintptr_t)kernel_text_start, &m) && m.flags == VMM_EXECUTE);
    REQUIRE(m.physical_address == boot->kernel_physical_base +
            (uintptr_t)kernel_text_start - boot->kernel_virtual_base);
    REQUIRE(!vmm_query(boot->direct_map_offset + m.physical_address, &m));
    REQUIRE(vmm_query((uintptr_t)kernel_rodata_start, &m) && m.flags == 0);
    REQUIRE(vmm_query((uintptr_t)kernel_data_start, &m) && m.flags == VMM_WRITE);
    const unsigned char *guards[] = {
        kernel_stack_guard_low, kernel_stack_guard_high,
        double_fault_stack_guard_low, double_fault_stack_guard_high,
        nmi_stack_guard_low, nmi_stack_guard_high,
        machine_check_stack_guard_low, machine_check_stack_guard_high,
    };
    for (unsigned int i = 0; i < sizeof(guards) / sizeof(guards[0]); ++i) {
        REQUIRE(!vmm_query((uintptr_t)guards[i], &m));
        const uint64_t physical = boot->kernel_physical_base + (uintptr_t)guards[i] - boot->kernel_virtual_base;
        REQUIRE(!vmm_query(boot->direct_map_offset + physical, &m));
    }
    const unsigned char *stacks[] = {
        kernel_stack_bottom, double_fault_stack_bottom, nmi_stack_bottom, machine_check_stack_bottom,
    };
    for (unsigned int i = 0; i < sizeof(stacks) / sizeof(stacks[0]); ++i) {
        REQUIRE(vmm_query((uintptr_t)stacks[i], &m) && m.flags == VMM_WRITE);
    }
    REQUIRE(vmm_query(boot->direct_map_offset + vmm_root_physical(), &m) && m.flags == VMM_WRITE);
    if (boot->framebuffer.address) {
        const struct boot_framebuffer *fb = &boot->framebuffer;
        const uint64_t first = fb->physical_address & ~UINT64_C(4095);
        const uint64_t end = (fb->physical_address + fb->pitch * fb->height + 4095) & ~UINT64_C(4095);
        for (uint64_t p = first; p < end; p += 4096) {
            REQUIRE(vmm_query(boot->direct_map_offset + p, &m));
            REQUIRE(m.physical_address == p && m.flags == VMM_WRITE);
            uint64_t physical = vmm_root_physical();
            for (unsigned int level = 4; level; --level) {
                const uint64_t *table = (const uint64_t *)(uintptr_t)(boot->direct_map_offset + physical);
                const uint64_t entry = table[((boot->direct_map_offset + p) >> (12 + 9 * (level - 1))) & 511];
                if (level == 1) {
                    REQUIRE((entry & 0x98) == 0x88 || (entry & 0x98) == 0x18);
                }
                physical = entry & UINT64_C(0x000ffffffffff000);
            }
        }
        REQUIRE(!vmm_map_page(VMM_WINDOW_BASE, first, VMM_WRITE));
        uint64_t removed;
        REQUIRE(!vmm_unmap_page(fb->address & ~UINT64_C(4095), &removed));
        REQUIRE(!vmm_protect_page(fb->address & ~UINT64_C(4095), 0));
        REQUIRE(!pmm_free_page(first));
        debug_write("[test] Framebuffer pages: reserved, RW/NX, device cache, no dynamic alias: OK\n");
    }
    debug_write("[test] VMM CR3, WP, NX, kernel permissions and 8 guards: OK\n");
    return true;
}

static bool check_mappings(const struct boot_info *boot) {
    const struct pmm_stats initial = pmm_get_stats();
    const uint64_t tables = vmm_table_pages();
    uint64_t first, second, removed;
    REQUIRE(pmm_alloc_page(&first) && pmm_alloc_page(&second));
    const uint64_t base = VMM_WINDOW_BASE;
    struct vmm_mapping m;
    REQUIRE(!vmm_init(boot));
    REQUIRE(!vmm_map_page(base + 1, first, 0));
    REQUIRE(!vmm_map_page(base, first + 1, 0));
    REQUIRE(!vmm_map_page(base, first, VMM_EXECUTE));
    REQUIRE(!vmm_map_page(base, 0, 0));
    REQUIRE(!vmm_map_page(0, first, 0));
    REQUIRE(!vmm_map_page(UINT64_C(0x0000800000000000), first, 0));
    REQUIRE(!vmm_map_page(base, UINT64_C(1) << 63, 0));
    REQUIRE(!vmm_map_page(base + VMM_WINDOW_SIZE, first, 0));
    REQUIRE(!vmm_map_page(base, boot->kernel_physical_base, VMM_WRITE));
    REQUIRE(!vmm_unmap_page((uintptr_t)kernel_text_start, &removed));
    REQUIRE(!vmm_protect_page((uintptr_t)kernel_text_start, VMM_WRITE));
    REQUIRE(vmm_table_pages() == tables);
    REQUIRE(vmm_map_page(base, first, VMM_WRITE));
    REQUIRE(!vmm_map_page(base, second, VMM_WRITE));
    REQUIRE(vmm_query(base + 37, &m) && m.physical_address == first + 37 && m.flags == VMM_WRITE);
    volatile uint64_t *alias = (volatile uint64_t *)(uintptr_t)base;
    volatile uint64_t *direct = (volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + first);
    alias[0] = UINT64_C(0xa1234567b89abcde);
    REQUIRE(direct[0] == alias[0]);
    REQUIRE(vmm_protect_page(base, 0) && vmm_query(base, &m) && m.flags == 0);
    REQUIRE(alias[0] == UINT64_C(0xa1234567b89abcde));
    REQUIRE(vmm_protect_page(base, VMM_WRITE));
    alias[0] = 55;
    REQUIRE(direct[0] == 55);
    REQUIRE(!vmm_unmap_page(base, 0));
    REQUIRE(vmm_unmap_page(base, &removed) && removed == first);
    REQUIRE(!vmm_query(base, &m) && !vmm_unmap_page(base, &removed));
    REQUIRE(!vmm_protect_page(base, 0));
    REQUIRE(vmm_table_pages() == tables);
    *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + second) = 77;
    REQUIRE(vmm_map_page(base, second, VMM_WRITE));
    REQUIRE(alias[0] == 77);
    REQUIRE(vmm_unmap_page(base, &removed) && removed == second);

    const uint64_t offsets[] = {0, 4096, 0x1ff000, 0x200000, 0x3ffff000, 0x40000000, VMM_WINDOW_SIZE - 4096};
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        REQUIRE(vmm_map_page(base + offsets[i], first, VMM_WRITE));
    }
    for (unsigned int i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        REQUIRE(vmm_query(base + offsets[i], &m) && m.physical_address == first);
        REQUIRE(*(volatile uint64_t *)(uintptr_t)(base + offsets[i]) == 55);
        REQUIRE(vmm_unmap_page(base + offsets[i], &removed) && removed == first);
    }
    REQUIRE(vmm_table_pages() == tables);
    REQUIRE(pmm_free_page(first) && pmm_free_page(second));
    REQUIRE(pmm_get_stats().free_pages == initial.free_pages);
    debug_write("[test] VMM map, protect, unmap, TLB and table reclamation: OK\n");
    return true;
}

static bool check_oom(const struct boot_info *boot) {
    const struct pmm_stats initial = pmm_get_stats();
    const uint64_t tables = vmm_table_pages();
    uint64_t frame;
    REQUIRE(pmm_alloc_page(&frame));
    /* A fresh PML4 branch needs three tables. Fail each allocation in turn. */
    for (unsigned int available = 0; available < 3; ++available) {
        uint64_t head = 0, page, count = 0;
        while (pmm_alloc_page(&page)) {
            REQUIRE(count < initial.free_pages);
            *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page) = head;
            head = page;
            ++count;
        }
        for (unsigned int i = 0; i < available; ++i) {
            REQUIRE(head != 0);
            page = head;
            head = *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
            REQUIRE(pmm_free_page(page));
            --count;
        }
        REQUIRE(!vmm_map_page(VMM_WINDOW_BASE, frame, VMM_WRITE));
        REQUIRE(pmm_get_stats().free_pages == available && vmm_table_pages() == tables);
        struct vmm_mapping m;
        REQUIRE(!vmm_query(VMM_WINDOW_BASE, &m));
        while (head != 0) {
            REQUIRE(count != 0);
            page = head;
            head = *(volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
            REQUIRE(pmm_free_page(page));
            --count;
        }
        REQUIRE(count == 0 && pmm_get_stats().free_pages == initial.free_pages - 1);
    }
    REQUIRE(vmm_map_page(VMM_WINDOW_BASE, frame, VMM_WRITE));
    uint64_t removed;
    REQUIRE(vmm_unmap_page(VMM_WINDOW_BASE, &removed) && removed == frame && pmm_free_page(frame));
    REQUIRE(vmm_table_pages() == tables && pmm_get_stats().free_pages == initial.free_pages);
    debug_write("[test] VMM allocation failure rollback at all 3 levels: OK\n");
    return true;
}

static bool check_mmio(const struct boot_info *boot) {
    const uint64_t free = pmm_get_stats().free_pages, tables = vmm_table_pages();
    uint64_t ram;
    REQUIRE(pmm_alloc_page(&ram));
    REQUIRE(!vmm_map_mmio_page(VMM_MMIO_BASE, ram));
    REQUIRE(!vmm_map_mmio_page(VMM_MMIO_BASE, boot->kernel_physical_base));
    REQUIRE(!vmm_map_mmio_page(VMM_WINDOW_BASE, 0xfec00000));
    REQUIRE(!vmm_map_mmio_page(VMM_MMIO_BASE + 1, 0xfec00000));
    REQUIRE(!vmm_map_mmio_page(VMM_MMIO_BASE + VMM_MMIO_SIZE, 0xfec00000));
    REQUIRE(vmm_map_mmio_page(VMM_MMIO_BASE, 0xfec00000)); /* QEMU IOAPIC page, no accesses. */
    REQUIRE(!vmm_map_mmio_page(VMM_MMIO_BASE, 0xfec01000));
    REQUIRE(!vmm_protect_page(VMM_MMIO_BASE, 0));
    uint64_t page = vmm_root_physical(), entry = 0;
    for (unsigned int level = 4; level > 0; --level) {
        const uint64_t *entries = (const uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
        entry = entries[(VMM_MMIO_BASE >> (12 + 9 * (level - 1))) & 511];
        REQUIRE(entry & 1);
        page = entry & UINT64_C(0x000ffffffffff000);
    }
    REQUIRE((entry & (UINT64_C(1) << 63)) && (entry & 0x1b) == 0x1b); /* NX, UC(PAT3), RW. */
    REQUIRE(vmm_unmap_mmio_page(VMM_MMIO_BASE));
    REQUIRE(!vmm_unmap_mmio_page(VMM_MMIO_BASE));
    REQUIRE(!vmm_unmap_mmio_page(VMM_WINDOW_BASE));
    REQUIRE(pmm_free_page(ram));
    REQUIRE(pmm_get_stats().free_pages == free && vmm_table_pages() == tables);
    debug_write("[test] MMIO window: UC/NX, RAM aliases rejected, table reclamation: OK\n");
    return true;
}

bool vmm_run_tests(const struct boot_info *boot) {
    return check_layout(boot) && check_mappings(boot) && check_oom(boot) && check_mmio(boot);
}
