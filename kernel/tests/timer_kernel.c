#include <stddef.h>
#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/pmm.h>
#include <malospal/timer.h>
#include <malospal/vmm.h>
#include "../src/arch/x86_64/exceptions.h"
#include "../src/arch/x86_64/io.h"

#define REQUIRE(test) do { \
    if (!(test)) { \
        debug_write("[test] Timer failure at line "); \
        debug_u64(__LINE__); \
        debug_write(": " #test "\n"); \
        return false; \
    } \
} while (0)

struct register_snapshot {
    uint64_t registers[15];
    uint64_t flags, rsp, stack_first, stack_second;
};
_Static_assert(offsetof(struct register_snapshot, flags) == 120, "Test flags offset");
_Static_assert(sizeof(struct register_snapshot) == 152, "Test snapshot size");

struct register_snapshot timer_test_snapshot;
uint64_t timer_test_saved_rsp, timer_test_expected_rsp;
extern void timer_test_capture(uint64_t padding);
extern const unsigned char timer_test_resume[];
static volatile uint64_t observed_frames;
static volatile bool bad_frame;

void timer_test_observe_irq(const struct exception_frame *frame) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if ((flags & 0x600) != 0 || frame->error != 0 || frame->vector != 32) {
        bad_frame = true;
    }
    if (frame->rip == (uintptr_t)timer_test_resume) {
        ++observed_frames;
        if (frame->rsp != timer_test_expected_rsp || frame->cs != 8 ||
            (frame->rflags & 0xfd7) != 0xed7) {
            bad_frame = true;
        }
    }
}

static uint16_t pic_isr(void) {
    outb(0x20, 0x0b);
    outb(0xa0, 0x0b);
    return (uint16_t)inb(0x20) | ((uint16_t)inb(0xa0) << 8);
}

bool timer_run_tests(void) {
    REQUIRE(!arch_irq_enabled() && timer_ticks() == 0);
    REQUIRE(!timer_init() && !arch_irqs_init());
    REQUIRE(!timer_wait_ticks(1) && !timer_wait_ticks(0));
    REQUIRE(inb(0x21) == 0xfe && inb(0xa1) == 0xff);
    /* Read back channel 0 status: low/high access, mode 2, binary. */
    outb(0x43, 0xe2);
    REQUIRE((inb(0x40) & 0x3f) == 0x34);
    const struct pmm_stats initial = pmm_get_stats();
    const uint64_t root = vmm_root_physical(), tables = vmm_table_pages();
    REQUIRE(pic_isr() == 0);
    __asm__ volatile ("int $0x27; int $0x2f" : : : "memory");
    REQUIRE(timer_ticks() == 0 && pic_isr() == 0 && !arch_irq_enabled());
    debug_write("[test] PIC masks, spurious vectors, PIT mode and init guards: OK\n");

    for (unsigned int i = 0; i < 16; ++i) {
        const uint64_t before = timer_ticks();
        const uint64_t frames = observed_frames;
        timer_test_capture((i & 1) * 8);
        REQUIRE(!arch_irq_enabled() && timer_ticks() > before);
        REQUIRE(observed_frames > frames && !bad_frame);
        for (unsigned int reg = 0; reg < 15; ++reg) {
            REQUIRE(timer_test_snapshot.registers[reg] == UINT64_C(0x1020304050607080) + reg);
        }
        REQUIRE((timer_test_snapshot.flags & 0xfd7) == 0xed7);
        REQUIRE(timer_test_snapshot.rsp == timer_test_expected_rsp);
        REQUIRE(timer_test_snapshot.stack_first == UINT64_C(0x5a6b7c8d9eaf0123));
        REQUIRE(timer_test_snapshot.stack_second == ~UINT64_C(0x5a6b7c8d9eaf0123));
        REQUIRE(pic_isr() == 0);
    }
    debug_write("[test] 16 real IRQ returns: 15 GPRs, RSP, RFLAGS/DF, stack, EOI: OK\n");

    arch_irq_enable();
    REQUIRE(arch_irq_enabled() && !timer_init() && !arch_irqs_init());
    REQUIRE(timer_wait_ticks(0) && !timer_wait_ticks(UINT64_MAX));
    const uint64_t start = timer_ticks();
    for (unsigned int i = 0; i < 100; ++i) {
        const uint64_t before = timer_ticks();
        REQUIRE(timer_wait_ticks(1) && timer_ticks() - before >= 1 && arch_irq_enabled());
    }
    const uint64_t outer = arch_irq_save();
    const uint64_t paused = timer_ticks();
    const uint64_t inner = arch_irq_save();
    arch_irq_restore(inner);
    REQUIRE(!arch_irq_enabled() && !timer_wait_ticks(1));
    /* Deliberately leave interrupts disabled over several PIT periods. */
    for (unsigned int i = 0; i < 1000000; ++i) {
        (void)inb(0x21);
    }
    REQUIRE(timer_ticks() == paused);
    arch_irq_restore(outer);
    REQUIRE(arch_irq_enabled() && timer_wait_ticks(3));
    arch_irq_save();
    REQUIRE(!bad_frame && pic_isr() == 0);
    REQUIRE(pmm_get_stats().free_pages == initial.free_pages);
    REQUIRE(pmm_get_stats().allocated_pages == initial.allocated_pages);
    REQUIRE(vmm_root_physical() == root && vmm_table_pages() == tables);
    debug_write("[test] Repeated sleep/wakeup, nested IF save/restore, no PMM/VMM changes: OK\n");
    debug_write("TIMER ticks=");
    debug_u64(timer_ticks());
    debug_write(" waited=");
    debug_u64(timer_ticks() - start);
    debug_write(" frames=");
    debug_u64(observed_frames);
    debug_write("\n");
    return true;
}
