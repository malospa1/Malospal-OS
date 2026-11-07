#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/layout.h>
#include "exceptions.h"

struct table_pointer {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp[3];
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

_Static_assert(sizeof(struct table_pointer) == 10, "GDTR/IDTR size");
_Static_assert(sizeof(struct tss64) == 104, "TSS size");
_Static_assert(offsetof(struct tss64, ist) == 36, "TSS IST offset");
_Static_assert(sizeof(struct idt_gate) == 16, "IDT gate size");

extern void x86_load_gdt(const struct table_pointer *pointer);
extern const uintptr_t x86_isr_table[256];

static uint64_t gdt[5] __attribute__((aligned(16))) = {
    0,
    0x00af9a000000ffff,
    0x00cf92000000ffff,
    0, 0,
};
static struct tss64 tss;
static struct idt_gate idt[256] __attribute__((aligned(16)));

void arch_exceptions_init(void) {
    __asm__ volatile ("cli" : : : "memory");
    tss.rsp[0] = (uintptr_t)kernel_stack_top;
    tss.ist[0] = (uintptr_t)double_fault_stack_top;
    tss.ist[1] = (uintptr_t)nmi_stack_top;
    tss.ist[2] = (uintptr_t)machine_check_stack_top;
    tss.iomap_base = sizeof(tss);

    const uint64_t base = (uintptr_t)&tss;
    const uint64_t limit = sizeof(tss) - 1;
    gdt[3] = (limit & 0xffff) | ((base & 0xffffff) << 16) |
             (0x89ull << 40) | (((limit >> 16) & 0xf) << 48) |
             (((base >> 24) & 0xff) << 56);
    gdt[4] = base >> 32;

    for (unsigned int vector = 0; vector < 256; ++vector) {
        const uintptr_t entry = x86_isr_table[vector];
        idt[vector] = (struct idt_gate){
            .offset_low = entry & 0xffff,
            .selector = 0x08,
            .ist = vector == 8 ? 1 : vector == 2 ? 2 : vector == 18 ? 3 : 0,
            .attributes = 0x8e,
            .offset_mid = (entry >> 16) & 0xffff,
            .offset_high = entry >> 32,
        };
    }

    const struct table_pointer gdtr = {sizeof(gdt) - 1, (uintptr_t)gdt};
    const struct table_pointer idtr = {sizeof(idt) - 1, (uintptr_t)idt};
    x86_load_gdt(&gdtr);
    __asm__ volatile ("lidt %0" : : "m"(idtr) : "memory");
}

static void field(const char *label, uint64_t value) {
    debug_write(label);
    debug_hex64(value);
}

_Noreturn void x86_exception_dispatch(const struct exception_frame *frame, uint64_t cr2) {
    static const char *const names[32] = {
        "Divide Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
        "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
        "Stack Segment Fault", "General Protection", "Page Fault", "Reserved",
        "x87 Floating Point", "Alignment Check", "Machine Check", "SIMD Floating Point",
        "Virtualization", "Control Protection", "Reserved", "Reserved",
        "Reserved", "Reserved", "Reserved", "Reserved",
        "Hypervisor Injection", "VMM Communication", "Security", "Reserved",
    };
    debug_write("\n[panic] CPU exception\nvector=");
    debug_u64(frame->vector);
    debug_write(" name=");
    debug_write(frame->vector < 32 ? names[frame->vector] : "Unexpected Interrupt");
    debug_write("\n");
    field("error=", frame->error);
    field(" rip=", frame->rip);
    field("\ncs=", frame->cs);
    field(" rflags=", frame->rflags);
    field("\nrsp=", frame->rsp);
    field(" ss=", frame->ss);
    field("\ncr2=", cr2);
    field("\nrax=", frame->rax); field(" rbx=", frame->rbx); field(" rcx=", frame->rcx);
    field("\nrdx=", frame->rdx); field(" rsi=", frame->rsi); field(" rdi=", frame->rdi);
    field("\nrbp=", frame->rbp); field(" r8=", frame->r8); field(" r9=", frame->r9);
    field("\nr10=", frame->r10); field(" r11=", frame->r11); field(" r12=", frame->r12);
    field("\nr13=", frame->r13); field(" r14=", frame->r14); field(" r15=", frame->r15);
    const uintptr_t address = (uintptr_t)frame;
    const uintptr_t df_start = (uintptr_t)double_fault_stack_bottom;
    debug_write("\nhandler_stack=");
    debug_write(address >= df_start && address + sizeof(*frame) <= (uintptr_t)double_fault_stack_top
                ? "double-fault-IST" : "other");
    debug_write("\nMALOSPAL_EXCEPTION_HALTED\n");
#ifdef MALOSPAL_FAULT_TEST
    arch_test_exit(0x11);
#elif defined(MALOSPAL_SMOKE_TEST) || defined(MALOSPAL_MEMORY_TEST) || defined(MALOSPAL_VIRTUAL_TEST) || defined(MALOSPAL_TIMER_TEST)
    arch_test_exit(0x12);
#endif
    arch_halt();
}
