#ifndef MALOSPAL_PMM_H
#define MALOSPAL_PMM_H

#include <malospal/boot.h>

#define PMM_PAGE_SIZE UINT64_C(4096)

struct pmm_stats {
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t metadata_pages;
    uint64_t metadata_base;
};

/* Boot CPU only, with interrupts disabled. Initialization may succeed once. */
bool pmm_init(const struct boot_info *info);
/* Returns a physical address, not a pointer. Contents are not zeroed. */
bool pmm_alloc_page(uint64_t *physical_address);
/* Only currently allocated pages may be returned; invalid frees change nothing. */
bool pmm_free_page(uint64_t physical_address);
struct pmm_stats pmm_get_stats(void);

#endif
