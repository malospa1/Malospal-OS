#include <malospal/arch.h>
#include <malospal/console.h>
#include <malospal/debug.h>
#include <malospal/game.h>
#include <malospal/keyboard.h>
#include <malospal/input.h>
#include <malospal/usb.h>
#include <malospal/pmm.h>
#include <malospal/serial.h>
#include <malospal/screen.h>
#include <malospal/timer.h>
#include <malospal/vmm.h>
#include "line.h"

static bool equal(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static void memory_command(void) {
    const uint64_t flags = arch_irq_save();
    const struct pmm_stats memory = pmm_get_stats();
    const uint64_t root = vmm_root_physical(), tables = vmm_table_pages();
    arch_irq_restore(flags);
    debug_write("PMM total="); debug_u64(memory.total_pages);
    debug_write(" free="); debug_u64(memory.free_pages);
    debug_write(" allocated="); debug_u64(memory.allocated_pages);
    debug_write(" metadata="); debug_u64(memory.metadata_pages);
    debug_write("\nVMM tables="); debug_u64(tables);
    debug_write(" root="); debug_hex64(root);
    debug_write("\n");
}

static void serial_command(void) {
    const struct serial_stats stats = serial_get_stats();
    debug_write("SERIAL received="); debug_u64(stats.received);
    debug_write(" queued="); debug_u64(stats.queued);
    debug_write(" dropped="); debug_u64(stats.dropped);
    debug_write(" errors="); debug_u64(stats.errors);
    debug_write(" irqs="); debug_u64(stats.interrupts);
    debug_write(" faulted="); debug_u64(stats.faulted);
    debug_write("\n");
}

static void keyboard_command(void) {
    const struct keyboard_stats stats = keyboard_get_stats();
    debug_write("KEYBOARD bytes="); debug_u64(stats.received);
    debug_write(" decoded="); debug_u64(stats.decoded);
    debug_write(" queued="); debug_u64(stats.queued);
    debug_write(" dropped="); debug_u64(stats.dropped);
    debug_write(" errors="); debug_u64(stats.errors);
    debug_write(" irqs="); debug_u64(stats.interrupts);
    debug_write(" ready="); debug_u64(stats.ready);
    debug_write(" faulted="); debug_u64(stats.faulted);
    debug_write("\n");
    const struct usb_stats usb = usb_get_stats();
    debug_write("USB controllers="); debug_u64(usb.controllers);
    debug_write(" keyboards="); debug_u64(usb.keyboards);
    debug_write(" reports="); debug_u64(usb.reports);
    debug_write(" decoded="); debug_u64(usb.decoded);
    debug_write(" queued="); debug_u64(usb.queued);
    debug_write(" dropped="); debug_u64(usb.dropped);
    debug_write(" errors="); debug_u64(usb.errors);
    debug_write(" unsupported="); debug_u64(usb.unsupported);
    debug_write(" failed="); debug_u64(usb.failed); debug_write("\n");
}

static void execute(char *text) {
    while (*text == ' ') {
        ++text;
    }
    if (*text == '\0') {
        return;
    }
    char *args = text;
    while (*args != ' ' && *args != '\0') {
        ++args;
    }
    if (*args != '\0') {
        *args++ = '\0';
        while (*args == ' ') {
            ++args;
        }
    }
    if (equal(text, "echo")) {
        debug_write(args);
        debug_write("\n");
        return;
    }
    if (*args != '\0') {
        debug_write("[error] Unexpected arguments; only echo accepts text\n");
        return;
    }
    if (equal(text, "help")) {
        debug_write("help    Command list\ninfo    Kernel information\nmem     Physical and virtual memory\n"
                    "ticks   Delivered timer ticks\nserial  Serial input counters\nkeyboard PS/2 and USB input counters\n"
                    "echo    Print text\nsnake   Play Snake\nslots   Play Reel Machine\nhalt    Stop the kernel\n");
    } else if (equal(text, "info")) {
        debug_write("Malospal OS 0.1.0 | own x86-64 kernel\nAuthor: malospal\nCPU: 1, ring 0; own GDT/IDT and page tables\n"
                    "Memory: 4 KiB pages, W^X, NX, stack guards\nDevices: PIC, PIT ~100 Hz\n");
        debug_write(serial_rx_ready() ? "Input: COM1 IRQ4\n" : "Input: COM1 unavailable\n");
        debug_write(keyboard_get_stats().ready ? "Input: PS/2 IRQ1 (US)\n" : "Input: PS/2 unavailable\n");
        if (usb_get_stats().controllers || usb_get_stats().failed) {
            debug_write(usb_ready() ? "Input: USB xHCI (US)\n" : "Input: USB unavailable\n");
        }
        debug_write(screen_ready() ? "Display: framebuffer\n" : "Display: serial only\n");
    } else if (equal(text, "mem")) {
        memory_command();
    } else if (equal(text, "ticks")) {
        debug_write("TIMER ticks="); debug_u64(timer_ticks());
        debug_write(" nominal_hz="); debug_u64(TIMER_HZ);
        debug_write("\n");
    } else if (equal(text, "serial")) {
        serial_command();
    } else if (equal(text, "keyboard")) {
        keyboard_command();
    } else if (equal(text, "snake")) {
        snake_run();
    } else if (equal(text, "slots")) {
        slots_run();
    } else if (equal(text, "halt")) {
        debug_write("Kernel halted.\n");
        arch_halt();
    } else {
        debug_write("[error] Unknown command: ");
        debug_write(text);
        debug_write("\n");
    }
}

_Noreturn void console_run(void) {
    struct console_line line = {0};
    unsigned int previous = INPUT_NONE;
    debug_write("Malospal OS console\nmalospal> ");
    for (;;) {
        uint8_t value = 0;
        unsigned int source = INPUT_NONE;
        const uint64_t flags = arch_irq_save();
        const enum serial_rx_result input = input_read(&value, &source, false);
        if (input == SERIAL_RX_EMPTY) {
            /* Check the queue and enter STI; HLT without a lost wakeup. */
            arch_irq_wait();
        }
        arch_irq_restore(flags);
        if (input == SERIAL_RX_EMPTY) {
            continue;
        }
        if (input == SERIAL_RX_LOST) {
            line_invalidate(&line);
            line.length = 0; /* The diagnostic starts a new visible line. */
            debug_write(source >= INPUT_KEYBOARD ? "\n[error] Keyboard input lost; current line discarded\n" :
                                                  "\n[error] Serial input lost; current line discarded\n");
            if (!input_ready()) {
                debug_write("[panic] No working console input after a device fault\n");
                arch_halt();
            }
            continue;
        }
        if (previous != INPUT_NONE && previous != source) {
            line.skip_lf = false;
            if (line.length && !line.rejected) {
                line_invalidate(&line);
                line.length = 0;
                debug_write("\n[error] Input source changed; current line discarded\n");
            }
        }
        previous = source;
        size_t old_length = line.length;
        switch (line_feed(&line, value)) {
            case LINE_ECHO: {
                const char echo[] = {value == '\t' ? ' ' : (char)value, '\0'};
                debug_write(echo);
                break;
            }
            case LINE_ERASE:
                debug_write("\b \b");
                break;
            case LINE_CLEAR:
                while (old_length-- != 0) {
                    debug_write("\b \b");
                }
                break;
            case LINE_SUBMIT:
                debug_write("\n");
                execute(line.text);
                debug_write("malospal> ");
                break;
            case LINE_REJECT:
                debug_write("\n[error] Line discarded: invalid, too long or input lost\nmalospal> ");
                break;
            case LINE_NONE:
                break;
        }
    }
}
