#include <malospal/arch.h>
#include <malospal/boot.h>
#include <malospal/console.h>
#include <malospal/debug.h>
#include <malospal/keyboard.h>
#include <malospal/usb.h>
#include <malospal/pmm.h>
#include <malospal/serial.h>
#include <malospal/screen.h>
#include <malospal/timer.h>
#include <malospal/vmm.h>

_Noreturn void kernel_main(void) {
    const bool serial_ready = debug_init();
    arch_exceptions_init();
    static struct boot_info info;
    const bool boot_ready = boot_read_info(&info);
    const bool display_ready = boot_ready && screen_init(&info.framebuffer);
    if (!serial_ready && !display_ready) { arch_halt(); }
    debug_write("\nMalospal OS 0.1.0 | own x86-64 kernel\nAuthor: malospal\n");
    debug_write("[ok] Assembly entry and private kernel stack\n");
    debug_write(serial_ready ? "[ok] Serial output\n" : "[warn] COM1 unavailable\n");
    if (display_ready) {
        debug_write("[ok] Screen output: "); debug_u64(info.framebuffer.width);
        debug_write("x"); debug_u64(info.framebuffer.height);
        debug_write("x"); debug_u64(info.framebuffer.bpp); debug_write("\n");
    } else { debug_write("[warn] No supported framebuffer; serial output only\n"); }
    debug_write("[ok] Own GDT, TSS and IDT; exception handlers ready\n");

    if (!boot_ready) {
        debug_write("[panic] Boot protocol or memory map is unavailable\n");
        arch_halt();
    }
    debug_write("[ok] Memory map: ");
    debug_u64(info.memory_region_count);
    debug_write(" regions, ");
    debug_u64(info.usable_memory_bytes / (1024 * 1024));
    debug_write(" MiB usable\n");
    if (!pmm_init(&info)) {
        debug_write("[panic] Physical memory initialization failed\n");
        arch_halt();
    }
#ifdef MALOSPAL_VIRTUAL_TEST
    extern bool vmm_test_initialization(const struct boot_info *boot);
    if (!vmm_test_initialization(&info)) {
        debug_write("[panic] Virtual memory initialization test failed\n");
        arch_test_exit(0x12);
        arch_halt();
    }
#endif
    if (!vmm_init(&info)) {
        debug_write("[panic] Virtual memory initialization failed (4-level paging and NX required)\n");
        arch_halt();
    }
    debug_write("[ok] Own page tables active; kernel W^X, NX data, 8 stack guards\nVMM tables=");
    debug_u64(vmm_table_pages());
    debug_write(" root=");
    debug_hex64(vmm_root_physical());
    debug_write("\n");
    const struct pmm_stats memory = pmm_get_stats();
    debug_write("[ok] Physical memory: ");
    debug_u64(memory.free_pages);
    debug_write(" free pages (4 KiB), ");
    debug_u64(memory.metadata_pages);
    debug_write(" metadata pages\nPMM total=");
    debug_u64(memory.total_pages);
    debug_write(" free=");
    debug_u64(memory.free_pages);
    debug_write(" allocated=");
    debug_u64(memory.allocated_pages);
    debug_write(" metadata=");
    debug_u64(memory.metadata_pages);
    debug_write("\n");
    debug_write("MALOSPAL_KERNEL_OK\n");

#ifdef MALOSPAL_VIRTUAL_TEST
    extern bool vmm_run_tests(const struct boot_info *boot);
    if (!vmm_run_tests(&info)) {
        debug_write("[panic] Virtual memory test failed\n");
        arch_test_exit(0x12);
        arch_halt();
    }
    debug_write("MALOSPAL_VMM_TEST_OK\n");
    arch_test_exit(0x10);
    arch_halt();
#endif
#ifdef MALOSPAL_MEMORY_TEST
    extern bool pmm_run_tests(const struct boot_info *boot);
    if (!pmm_run_tests(&info)) {
        debug_write("[panic] Physical memory test failed\n");
        arch_test_exit(0x12);
        arch_halt();
    }
    debug_write("MALOSPAL_PMM_TEST_OK\n");
    arch_test_exit(0x10);
    arch_halt();
#endif
#ifdef MALOSPAL_FAULT_TEST
    extern void arch_trigger_test_exception(void);
    extern unsigned char arch_test_fault_site[];
#if MALOSPAL_FAULT_TEST >= 100
    extern uint64_t arch_test_target;
    extern bool vmm_prepare_fault(const struct boot_info *boot);
    if (!vmm_prepare_fault(&info)) {
        debug_write("[panic] Virtual memory fault setup failed\n");
        arch_test_exit(0x12);
        arch_halt();
    }
    debug_write("[test] Expected fault address=");
    debug_hex64(arch_test_target);
    debug_write("\n");
#endif
    debug_write("[test] Expected fault RIP=");
#if MALOSPAL_FAULT_TEST == 101
    debug_hex64(arch_test_target);
#else
    debug_hex64((uintptr_t)arch_test_fault_site);
#endif
    debug_write("\n");
    arch_trigger_test_exception();
    debug_write("[panic] Exception test unexpectedly returned\n");
    arch_halt();
#endif
    if (!arch_irqs_init() || !timer_init()) {
        debug_write("[panic] PIC/PIT initialization failed\n");
#if defined(MALOSPAL_SMOKE_TEST) || defined(MALOSPAL_TIMER_TEST)
        arch_test_exit(0x12);
#endif
        arch_halt();
    }
    debug_write("[ok] PIC IRQs and PIT timer ready (~100 Hz)\n");
#ifdef MALOSPAL_TIMER_TEST
    extern bool timer_run_tests(void);
    if (!timer_run_tests()) {
        debug_write("[panic] Timer test failed\n");
        arch_test_exit(0x12);
        arch_halt();
    }
    debug_write("MALOSPAL_TIMER_TEST_OK\n");
    arch_test_exit(0x10);
    arch_halt();
#endif
    arch_irq_enable();
#ifdef MALOSPAL_SMOKE_TEST
    if (!timer_wait_ticks(3)) {
        arch_test_exit(0x12);
        arch_halt();
    }
    debug_write("MALOSPAL_TIMER_RUNNING\n");
    arch_test_exit(0x10);
    arch_halt();
#endif
    /* Take ownership from firmware USB emulation before touching i8042. */
    const bool usb_input = usb_init(&info);
    const uint64_t flags = arch_irq_save();
    const bool serial_input = serial_ready && serial_rx_init();
    const bool keyboard_input = keyboard_init_timed();
    arch_irq_restore(flags);
    debug_write(serial_input ? "[ok] Serial input ready (COM1 IRQ4)\n" :
                               "[warn] Serial input unavailable\n");
    debug_write(keyboard_input ? "[ok] PS/2 input ready (IRQ1, set 2, US)\n" :
                                 "[warn] PS/2 keyboard unavailable\n");
    if (!keyboard_input) {
        const struct keyboard_diagnostics d = keyboard_get_diagnostics();
        debug_write("[ps2] "); debug_write(d.stage);
        debug_write(" status="); debug_hex64(d.status);
        debug_write(" reply="); debug_hex64(d.reply); debug_write("\n");
    }
    if (usb_input) { debug_write("[ok] USB keyboard input ready (xHCI, boot HID, US)\n"); }
    if (!serial_input && !keyboard_input && !usb_input) {
        debug_write("Kernel ready. No input device; screen output only.\n");
        for (;;) {
            arch_irq_save();
            arch_irq_wait();
        }
    }
    console_run();
}
