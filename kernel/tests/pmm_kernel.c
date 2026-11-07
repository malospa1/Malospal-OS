#include <malospal/debug.h>
#include <malospal/pmm.h>

#define REQUIRE(test) do { \
    if (!(test)) { \
        debug_write("[test] PMM failure at line "); \
        debug_u64(__LINE__); \
        debug_write(": " #test "\n"); \
        return false; \
    } \
} while (0)

static bool usable_page(const struct boot_info *boot, uint64_t page) {
    if (page == 0 || page % PMM_PAGE_SIZE != 0) {
        return false;
    }
    bool usable = false;
    for (uint64_t i = 0; i < boot->memory_region_count; ++i) {
        const struct boot_memory_region *r = &boot->regions[i];
        if (r->length == 0) {
            continue;
        }
        if (page < r->base + r->length && r->base < page + PMM_PAGE_SIZE) {
            if (r->type != BOOT_MEMORY_USABLE || page < r->base ||
                PMM_PAGE_SIZE > r->length || page - r->base > r->length - PMM_PAGE_SIZE) {
                return false;
            }
            usable = true;
        }
    }
    return usable;
}

static volatile uint64_t *page_words(const struct boot_info *boot, uint64_t page) {
    return (volatile uint64_t *)(uintptr_t)(boot->direct_map_offset + page);
}

bool pmm_run_tests(const struct boot_info *boot) {
    const struct pmm_stats initial = pmm_get_stats();
    REQUIRE(initial.free_pages > 2);
    REQUIRE(!pmm_free_page(0) && !pmm_free_page(initial.metadata_base));
    REQUIRE(!pmm_alloc_page(0));
    uint64_t first, second, recycled;
    REQUIRE(pmm_alloc_page(&first) && pmm_alloc_page(&second));
    REQUIRE(first != second && usable_page(boot, first) && usable_page(boot, second));
    REQUIRE(!pmm_free_page(first + 1));
    volatile uint64_t *a = page_words(boot, first);
    volatile uint64_t *b = page_words(boot, second);
    for (uint64_t i = 0; i < PMM_PAGE_SIZE / sizeof(uint64_t); ++i) {
        a[i] = UINT64_C(0x55aa0123456789ab) ^ i;
        b[i] = UINT64_C(0xaa55fedcba987654) ^ i;
    }
    for (uint64_t i = 0; i < PMM_PAGE_SIZE / sizeof(uint64_t); ++i) {
        REQUIRE(a[i] == (UINT64_C(0x55aa0123456789ab) ^ i));
        REQUIRE(b[i] == (UINT64_C(0xaa55fedcba987654) ^ i));
    }
    REQUIRE(pmm_free_page(first) && !pmm_free_page(first));
    REQUIRE(pmm_alloc_page(&recycled) && recycled == first);
    REQUIRE(pmm_free_page(recycled) && pmm_free_page(second));
    REQUIRE(pmm_get_stats().free_pages == initial.free_pages);
    debug_write("[test] PMM allocation, writes, reuse and invalid frees: OK\n");

    /* Store the live allocation list inside the pages, without a test heap. */
    uint64_t head = 0, count = 0, page;
    const uint64_t tail = PMM_PAGE_SIZE / sizeof(uint64_t) - 1;
    while (pmm_alloc_page(&page)) {
        REQUIRE(count < initial.free_pages && usable_page(boot, page));
        REQUIRE(page < initial.metadata_base ||
                page - initial.metadata_base >= initial.metadata_pages * PMM_PAGE_SIZE);
        volatile uint64_t *words = page_words(boot, page);
        words[0] = head;
        words[1] = page;
        words[tail] = ~page;
        head = page;
        ++count;
    }
    REQUIRE(count == initial.free_pages && pmm_get_stats().free_pages == 0);
    REQUIRE(pmm_get_stats().allocated_pages == initial.allocated_pages + count);
    page = UINT64_MAX;
    REQUIRE(!pmm_alloc_page(&page) && page == UINT64_MAX);
    REQUIRE(pmm_free_page(head));
    REQUIRE(pmm_alloc_page(&page) && page == head);
    uint64_t returned = 0;
    while (head != 0) {
        REQUIRE(returned < count && usable_page(boot, head));
        volatile uint64_t *words = page_words(boot, head);
        REQUIRE(words[1] == head && words[tail] == ~head);
        const uint64_t next = words[0];
        REQUIRE(pmm_free_page(head) && !pmm_free_page(head));
        head = next;
        ++returned;
    }
    REQUIRE(returned == count && pmm_get_stats().free_pages == initial.free_pages);
    REQUIRE(pmm_get_stats().allocated_pages == initial.allocated_pages);
    debug_write("[test] PMM exhausted and restored ");
    debug_u64(count);
    debug_write(" pages: OK\n");
    return true;
}
