#ifndef MALOSPAL_VMM_H
#define MALOSPAL_VMM_H

#include <malospal/boot.h>

#define VMM_WINDOW_BASE UINT64_C(0xffffc00000000000)
#define VMM_WINDOW_SIZE UINT64_C(0x80000000)
#define VMM_WRITE 1u
#define VMM_EXECUTE 2u
#define VMM_MMIO_BASE UINT64_C(0xffffc00100000000)
#define VMM_MMIO_SIZE UINT64_C(0x1000000)

struct vmm_mapping {
    uint64_t physical_address;
    unsigned int flags;
};

/* Single CPU, interrupts disabled. Own 4-level tables; NX is required. */
bool vmm_init(const struct boot_info *boot);
/* Dynamic window only. Flags are 0 (read-only) or VMM_WRITE; always NX. */
bool vmm_map_page(uint64_t virtual_address, uint64_t physical_address, unsigned int flags);
bool vmm_protect_page(uint64_t virtual_address, unsigned int flags);
/* Unmap returns the frame but does not free it. Empty page tables are freed. */
bool vmm_unmap_page(uint64_t virtual_address, uint64_t *physical_address);
/* Separate device window, UC, writable, NX. Rejects RAM/firmware/framebuffer aliases. */
bool vmm_map_mmio_page(uint64_t virtual_address, uint64_t physical_address);
bool vmm_unmap_mmio_page(uint64_t virtual_address);
bool vmm_query(uint64_t virtual_address, struct vmm_mapping *mapping);
uint64_t vmm_root_physical(void);
uint64_t vmm_table_pages(void);

#endif
