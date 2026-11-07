#include <malospal/pmm.h>
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { \
    if (!(test)) { \
        fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #test); \
        return 1; \
    } \
} while (0)

#define P PMM_PAGE_SIZE
static unsigned char arena[4 * PMM_PAGE_SIZE] __attribute__((aligned(4096)));
static struct boot_info info;

static void setup(uint64_t metadata_base) {
    memset(&info, 0, sizeof(info));
    memset(arena, 0xa5, sizeof(arena));
    info.direct_map_offset = (uintptr_t)(arena + P) - metadata_base;
}

static void region(uint64_t base, uint64_t length, enum boot_memory_type type) {
    info.regions[info.memory_region_count++] = (struct boot_memory_region){base, length, type};
}

static int guards(uint64_t metadata_pages) {
    for (uint64_t i = 0; i < P; ++i) {
        CHECK(arena[i] == 0xa5);
    }
    for (uint64_t i = (metadata_pages + 1) * P; i < sizeof(arena); ++i) {
        CHECK(arena[i] == 0xa5);
    }
    return 0;
}

static int basic(void) {
    uint64_t page = 123;
    CHECK(!pmm_alloc_page(&page) && page == 123);
    CHECK(!pmm_free_page(P));
    CHECK(pmm_get_stats().total_pages == 0);
    setup(P);
    region(0, 8 * P, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().total_pages == 7);
    CHECK(pmm_get_stats().free_pages == 6);
    CHECK(pmm_get_stats().metadata_base == P);
    CHECK(!pmm_free_page(0) && !pmm_free_page(P) && !pmm_free_page(2 * P));
    CHECK(!pmm_alloc_page(0));
    for (uint64_t i = 2; i < 8; ++i) {
        CHECK(pmm_alloc_page(&page) && page == i * P);
    }
    page = 123;
    CHECK(!pmm_alloc_page(&page) && page == 123);
    CHECK(pmm_get_stats().free_pages == 0 && pmm_get_stats().allocated_pages == 6);
    CHECK(!pmm_free_page(2 * P + 1) && !pmm_free_page(8 * P));
    CHECK(pmm_free_page(4 * P) && !pmm_free_page(4 * P));
    CHECK(pmm_alloc_page(&page) && page == 4 * P);
    CHECK(!pmm_init(&info));
    CHECK(pmm_get_stats().allocated_pages == 6);
    for (uint64_t i = 2; i < 8; ++i) {
        CHECK(pmm_free_page(i * P));
    }
    CHECK(pmm_get_stats().free_pages == 6 && pmm_get_stats().allocated_pages == 0);
    return guards(1);
}

static int sparse(void) {
    setup(2 * P);
    region(0, 2 * P, BOOT_MEMORY_RESERVED);
    region(2 * P, 5 * P, BOOT_MEMORY_USABLE);
    region(7 * P, 2 * P, BOOT_MEMORY_KERNEL_MODULES);
    region(9 * P, P, BOOT_MEMORY_BOOTLOADER);
    region(10 * P, P, BOOT_MEMORY_ACPI_RECLAIMABLE);
    region(11 * P, P, BOOT_MEMORY_ACPI_NVS);
    region(12 * P, P, BOOT_MEMORY_BAD);
    region(13 * P, P, BOOT_MEMORY_FRAMEBUFFER);
    region(14 * P, P, (enum boot_memory_type)99);
    const uint64_t high = UINT64_C(1) << 40;
    region(high, 3 * P, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().total_pages == 8 && pmm_get_stats().metadata_pages == 1);
    uint64_t page;
    for (uint64_t i = 3; i < 7; ++i) {
        CHECK(pmm_alloc_page(&page) && page == i * P);
    }
    for (uint64_t i = 0; i < 3; ++i) {
        CHECK(pmm_alloc_page(&page) && page == high + i * P);
    }
    CHECK(!pmm_alloc_page(&page));
    for (uint64_t i = 7; i < 16; ++i) {
        CHECK(!pmm_free_page(i * P));
    }
    CHECK(pmm_free_page(high) && pmm_alloc_page(&page) && page == high);
    CHECK(pmm_free_page(3 * P) && pmm_alloc_page(&page) && page == 3 * P);
    return guards(1);
}

static int unaligned(void) {
    setup(P);
    region(1, 4 * P - 2, BOOT_MEMORY_USABLE);
    region(5 * P + 1, P, BOOT_MEMORY_USABLE);
    region(8 * P, 0, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().total_pages == 2);
    uint64_t page;
    CHECK(pmm_alloc_page(&page) && page == 2 * P);
    CHECK(!pmm_alloc_page(&page));
    CHECK(!pmm_free_page(3 * P) && !pmm_free_page(6 * P));
    return guards(1);
}

static int metadata(void) {
    setup(3 * P);
    region(P, P, BOOT_MEMORY_USABLE);
    region(3 * P, 32769 * P, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().metadata_pages == 2 && pmm_get_stats().metadata_base == 3 * P);
    CHECK(pmm_get_stats().free_pages == 32768);
    uint64_t page;
    CHECK(pmm_alloc_page(&page) && page == P);
    for (uint64_t i = 5; i < 32772; ++i) {
        CHECK(pmm_alloc_page(&page) && page == i * P);
    }
    CHECK(!pmm_alloc_page(&page));
    CHECK(!pmm_free_page(3 * P) && !pmm_free_page(4 * P));
    CHECK(pmm_free_page(P));
    for (uint64_t i = 5; i < 32772; ++i) {
        CHECK(pmm_free_page(i * P));
    }
    CHECK(pmm_get_stats().free_pages == 32768);
    return guards(2);
}

static int invalid(void) {
    CHECK(!pmm_init(0));
    setup(P);
    CHECK(!pmm_init(&info));
    info.memory_region_count = BOOT_MAX_MEMORY_REGIONS + 1;
    CHECK(!pmm_init(&info));
    setup(P);
    region(UINT64_MAX - P, 2 * P, BOOT_MEMORY_USABLE);
    CHECK(!pmm_init(&info));
    setup(P);
    region(2 * P, P, BOOT_MEMORY_USABLE);
    region(P, P, BOOT_MEMORY_RESERVED);
    CHECK(!pmm_init(&info));
    setup(P);
    region(P, 4 * P, BOOT_MEMORY_USABLE);
    region(3 * P, P, BOOT_MEMORY_RESERVED);
    CHECK(!pmm_init(&info));
    info.regions[1].type = BOOT_MEMORY_USABLE;
    CHECK(!pmm_init(&info));
    setup(P);
    region(0, 4 * P, BOOT_MEMORY_RESERVED);
    region(P, 4 * P, BOOT_MEMORY_USABLE);
    CHECK(!pmm_init(&info));
    setup(P);
    region(P, P, BOOT_MEMORY_USABLE);
    CHECK(!pmm_init(&info));
    setup(P);
    region(P + 1, P - 2, BOOT_MEMORY_USABLE);
    CHECK(!pmm_init(&info));
    setup(P);
    region(P, 4 * P, BOOT_MEMORY_USABLE);
    info.direct_map_offset = UINT64_MAX - P + 1;
    CHECK(!pmm_init(&info));
    info.direct_map_offset = 1;
    CHECK(!pmm_init(&info));
    CHECK(pmm_get_stats().total_pages == 0);
    CHECK(guards(0) == 0);
    setup(4 * P);
    region(0, 3 * P, BOOT_MEMORY_RESERVED);
    region(P, P, BOOT_MEMORY_ACPI_NVS);
    region(4 * P, 4 * P, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().free_pages == 3);
    return guards(1);
}

static int fragmented(void) {
    setup(P);
    for (uint64_t i = 0; i < BOOT_MAX_MEMORY_REGIONS; ++i) {
        region((1 + i * 2) * P, P, BOOT_MEMORY_USABLE);
    }
    CHECK(pmm_init(&info));
    CHECK(pmm_get_stats().total_pages == BOOT_MAX_MEMORY_REGIONS);
    uint64_t page;
    for (uint64_t i = 1; i < BOOT_MAX_MEMORY_REGIONS; ++i) {
        CHECK(pmm_alloc_page(&page) && page == (1 + i * 2) * P);
    }
    CHECK(!pmm_alloc_page(&page));
    for (uint64_t i = 1; i < BOOT_MAX_MEMORY_REGIONS; ++i) {
        CHECK(pmm_free_page((1 + i * 2) * P));
    }
    CHECK(pmm_get_stats().free_pages == BOOT_MAX_MEMORY_REGIONS - 1);
    return guards(1);
}

static int mixed(void) {
    setup(P);
    region(P, 100 * P, BOOT_MEMORY_USABLE);
    CHECK(pmm_init(&info));
    uint64_t slots[99] = {0};
    uint32_t random = 0x81234567;
    uint64_t used = 0;
    for (unsigned int step = 0; step < 20000; ++step) {
        random = random * 1664525u + 1013904223u;
        const unsigned int slot = random % 99;
        if (slots[slot] != 0) {
            CHECK(pmm_free_page(slots[slot]));
            slots[slot] = 0;
            --used;
        } else {
            uint64_t page;
            CHECK(pmm_alloc_page(&page));
            CHECK(page >= 2 * P && page < 101 * P && page % P == 0);
            for (unsigned int j = 0; j < 99; ++j) {
                CHECK(page != slots[j]);
            }
            slots[slot] = page;
            ++used;
        }
        CHECK(pmm_get_stats().allocated_pages == used);
        CHECK(pmm_get_stats().free_pages == 99 - used);
    }
    for (unsigned int i = 0; i < 99; ++i) {
        if (slots[i] != 0) {
            CHECK(pmm_free_page(slots[i]));
        }
    }
    CHECK(pmm_get_stats().free_pages == 99);
    return guards(1);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const struct { const char *name; int (*test)(void); } cases[] = {
        {"basic", basic}, {"sparse", sparse}, {"unaligned", unaligned},
        {"metadata", metadata}, {"invalid", invalid}, {"fragmented", fragmented},
        {"mixed", mixed},
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            const int result = cases[i].test();
            if (result == 0) {
                printf("PASS: PMM host %s\n", cases[i].name);
            }
            return result;
        }
    }
    return 1;
}
