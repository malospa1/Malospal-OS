/* Minimal polling xHCI host: USB 2 boot keyboards on directly connected root ports.
 * Intel xHCI 1.2, chapters 4-7; USB HID 1.11, boot protocol. No firmware calls.
 * One command/control TD in flight per controller; eight interrupt TDs per keyboard.
 */
#include <malospal/arch.h>
#include <malospal/debug.h>
#include <malospal/pci.h>
#include <malospal/pmm.h>
#include <malospal/timer.h>
#include <malospal/usb.h>
#include <malospal/vmm.h>
#include "hid.h"

enum { MAX_HOSTS = 4, MAX_KEYS = 8, MAX_PAGES = 128, MMIO_BYTES = 65536,
       TRBS = 256, QUEUE_SIZE = 256, TIMEOUT = 200, REPORTS = 8 };
#define TYPE(n) ((uint32_t)(n) << 10)
#define PORT_CHANGES (UINT32_C(0x7f) << 17)
#define PORT_KEEP ((1u << 9) | (3u << 14) | (7u << 25))

struct trb { uint64_t pointer; uint32_t status, control; };
struct ring { volatile struct trb *entries; uint64_t physical; unsigned int at, cycle; };
struct host;
struct device {
    struct host *host;
    unsigned int id, slot, port, speed, endpoint;
    bool ready;
    uint64_t input_phys, output_phys, data_phys, report_phys, pending[REPORTS];
    uint32_t *input, *output;
    uint8_t *data, *report;
    struct ring control, interrupt;
    struct hid_keyboard decoder;
};
struct host {
    uintptr_t base, op, runtime, doorbell;
    uint16_t bdf, original_command;
    unsigned int ports, slots, context_size, mapped, page_count, event_at, event_cycle;
    uint8_t protocol[256], slot_type[256];
    bool wide, owned, live, failed, counted;
    uint64_t pages[MAX_PAGES], dcbaa_phys, event_phys, erst_phys;
    volatile uint64_t *dcbaa;
    volatile struct trb *events;
    struct ring command;
    uint64_t command_wait, control_data, control_status;
    unsigned int command_code, command_slot, control_slot, control_code, control_length;
    const char *stage;
    struct device devices[MAX_KEYS];
};
static struct host hosts[MAX_HOSTS];
static unsigned int host_count, head, tail;
static struct { uint8_t key, device; } queue[QUEUE_SIZE];
static bool attempted, lost;
static uint64_t direct_map;
static struct usb_stats stats;

static uint32_t read32(uintptr_t address) { return *(volatile uint32_t *)address; }
static void write32(uintptr_t address, uint32_t value) { *(volatile uint32_t *)address = value; }
static void write64(uintptr_t address, uint64_t value) {
    write32(address, (uint32_t)value);
    write32(address + 4, (uint32_t)(value >> 32));
}
static void barrier(void) { __asm__ volatile ("mfence" : : : "memory"); }
static void zero(void *buffer, unsigned int length) {
    uint8_t *p = buffer;
    for (unsigned int i = 0; i < length; ++i) { p[i] = 0; }
}
static bool wait_bits(uintptr_t address, uint32_t mask, uint32_t value, uint64_t timeout) {
    const uint64_t start = timer_ticks();
    do {
        const uint32_t current = read32(address);
        if (current == UINT32_MAX) { return false; }
        if ((current & mask) == value) { return true; }
        timer_wait_ticks(1);
    } while (timer_ticks() - start < timeout);
    return false;
}

static void *dma_page(struct host *h, uint64_t *physical) {
    if (h->page_count == MAX_PAGES) { return 0; }
    const uint64_t flags = arch_irq_save();
    bool ok = pmm_alloc_page(physical);
    if (ok && !h->wide && *physical > UINT32_MAX - (PMM_PAGE_SIZE - 1)) {
        pmm_free_page(*physical); ok = false;
    }
    arch_irq_restore(flags);
    if (!ok) { return 0; }
    h->pages[h->page_count++] = *physical;
    void *page = (void *)(uintptr_t)(direct_map + *physical);
    zero(page, PMM_PAGE_SIZE);
    return page;
}
static void free_after(struct host *h, unsigned int checkpoint) {
    const uint64_t flags = arch_irq_save();
    while (h->page_count > checkpoint) { pmm_free_page(h->pages[--h->page_count]); }
    arch_irq_restore(flags);
}
static bool ring_init(struct host *h, struct ring *ring) {
    ring->entries = dma_page(h, &ring->physical);
    ring->at = 0; ring->cycle = 1;
    return ring->entries != 0;
}
static uint64_t enqueue(struct ring *r, uint64_t pointer, uint32_t status, uint32_t control) {
    const unsigned int at = r->at;
    r->entries[at].pointer = pointer;
    r->entries[at].status = status;
    __asm__ volatile ("" : : : "memory");
    r->entries[at].control = control | r->cycle;
    if (++r->at == TRBS - 1) {
        r->entries[TRBS - 1].pointer = r->physical;
        r->entries[TRBS - 1].status = 0;
        __asm__ volatile ("" : : : "memory");
        r->entries[TRBS - 1].control = TYPE(6) | 2 | r->cycle; /* Link, toggle cycle. */
        r->at = 0; r->cycle ^= 1;
    }
    return r->physical + at * sizeof(struct trb);
}
static void doorbell(struct host *h, unsigned int slot, unsigned int endpoint) {
    barrier();
    write32(h->doorbell + slot * 4, endpoint);
    (void)read32(h->doorbell); /* Flush posted writes. */
}
static void lose_input(void) {
    stats.dropped += (head - tail) & (QUEUE_SIZE - 1);
    tail = head;
    lost = true;
}
static void emit(void *context, uint8_t key) {
    struct device *d = context;
    const unsigned int next = (head + 1) & (QUEUE_SIZE - 1);
    if (next == tail) { lose_input(); ++stats.dropped; return; }
    queue[head].key = key;
    queue[head].device = (uint8_t)d->id;
    head = next;
    ++stats.decoded;
}
static void device_fault(struct device *d) {
    if (!d->ready) { return; }
    d->ready = false;
    --stats.keyboards;
    ++stats.errors;
    lose_input();
    d->decoder.repeat = 0;
}
static void host_fault(struct host *h) {
    if (h->failed) { return; }
    h->failed = true;
    ++stats.failed;
    if (h->counted) { --stats.controllers; h->counted = false; }
    for (unsigned int i = 0; i < MAX_KEYS; ++i) { device_fault(&h->devices[i]); }
    /* Nonblocking failure path: stop bus mastering and retain DMA pages until reboot. */
    pci_command(h->bdf, (uint16_t)((pci_read(h->bdf, 4) | 0x400) & ~4u));
    if (h->owned) { write32(h->op, 0); }
}
static void arm(struct device *d, unsigned int report) {
    d->pending[report] = enqueue(&d->interrupt, d->report_phys + report * 8, 8, TYPE(1) | (1u << 5) | (1u << 2));
    doorbell(d->host, d->slot, d->endpoint);
}

static void poll_host(struct host *h) {
    if (!h->live || h->failed) { return; }
    const uint32_t status = read32(h->op + 4);
    if (status & ((1u << 2) | (1u << 12) | 1)) { host_fault(h); return; }
    for (unsigned int count = 0; count < 64; ++count) {
        volatile struct trb *source = &h->events[h->event_at];
        const uint32_t control = source->control;
        if ((control & 1) != h->event_cycle) { break; }
        __asm__ volatile ("" : : : "memory");
        const uint64_t pointer = source->pointer;
        const uint32_t result = source->status;
        const unsigned int type = (control >> 10) & 63, code = result >> 24, slot = control >> 24;
        if (++h->event_at == TRBS) { h->event_at = 0; h->event_cycle ^= 1; }
        if (type == 33 && (pointer & ~UINT64_C(15)) == h->command_wait) {
            h->command_code = code;
            h->command_slot = slot;
        } else if (type == 32) {
            const unsigned int endpoint = (control >> 16) & 31;
            const uint64_t trb = pointer & ~UINT64_C(15);
            if (slot == h->control_slot && endpoint == 1) {
                if (code != 1 && code != 13) { h->control_code = code; }
                else if (trb == h->control_data) {
                    const unsigned int remaining = result & 0xffffff;
                    if (remaining > h->control_length) { h->control_code = 255; }
                    else { h->control_length -= remaining; }
                } else if (trb == h->control_status) { h->control_code = 1; }
            } else {
                for (unsigned int i = 0; i < MAX_KEYS; ++i) {
                    struct device *d = &h->devices[i];
                    if (!d->ready || d->slot != slot || d->endpoint != endpoint) { continue; }
                    unsigned int report = 0;
                    while (report < REPORTS && d->pending[report] != trb) { ++report; }
                    if (report == REPORTS) { device_fault(d); break; }
                    if ((code != 1 && code != 13) || (result & 0xffffff) > 8) { device_fault(d); break; }
                    ++stats.reports;
                    if (!hid_report(&d->decoder, d->report + report * 8, 8 - (result & 0xffffff), timer_ticks(), emit, d)) {
                        ++stats.errors; lose_input();
                    }
                    arm(d, report);
                    break;
                }
            }
        } else if (type == 37) { host_fault(h); break; }
        /* Port status events are acknowledged below; hot-add requires reboot. */
    }
    write64(h->runtime + 0x38, (h->event_phys + h->event_at * 16) | 8);
    for (unsigned int i = 0; i < MAX_KEYS; ++i) {
        struct device *d = &h->devices[i];
        if (!d->ready) { continue; }
        const uint32_t port = read32(h->op + 0x400 + (d->port - 1) * 16);
        /* A disconnect/reconnect between polls is still loss: never reuse a stale slot. */
        if ((port & 3) != 3 || (port & ((1u << 17) | (1u << 3)))) { device_fault(d); }
        else { hid_repeat(&d->decoder, timer_ticks(), emit, d); }
    }
}

static bool command(struct host *h, uint64_t pointer, uint32_t control, unsigned int *slot) {
    if (h->failed) { return false; }
    h->command_code = h->command_slot = 0;
    h->command_wait = enqueue(&h->command, pointer, 0, control);
    doorbell(h, 0, 0);
    const uint64_t start = timer_ticks();
    do {
        poll_host(h);
        if (h->failed) { return false; }
        if (h->command_code) {
            if (slot) { *slot = h->command_slot; }
            return h->command_code == 1;
        }
        timer_wait_ticks(1);
    } while (timer_ticks() - start < TIMEOUT);
    host_fault(h); /* Do not overwrite a command whose completion is unknown. */
    return false;
}

static bool control_in(struct device *d, uint8_t type, uint8_t request, uint16_t value,
                       uint16_t index, uint16_t length, unsigned int *actual) {
    struct host *h = d->host;
    if (h->failed || length > PMM_PAGE_SIZE) { return false; }
    zero(d->data, PMM_PAGE_SIZE);
    h->control_code = 0; h->control_slot = d->slot;
    h->control_length = length; h->control_data = 0;
    const uint64_t setup = type | (uint64_t)request << 8 | (uint64_t)value << 16 |
                           (uint64_t)index << 32 | (uint64_t)length << 48;
    enqueue(&d->control, setup, 8, TYPE(2) | (1u << 6) | (length ? 3u << 16 : 0));
    if (length) {
        h->control_data = enqueue(&d->control, d->data_phys, length, TYPE(3) | (1u << 16) | (1u << 2));
    }
    h->control_status = enqueue(&d->control, 0, 0, TYPE(4) | (1u << 5) | (length ? 0 : 1u << 16));
    doorbell(h, d->slot, 1);
    const uint64_t start = timer_ticks();
    do {
        poll_host(h);
        if (h->failed) { return false; }
        if (h->control_code) {
            h->control_slot = 0;
            if (actual) { *actual = h->control_length; }
            return h->control_code == 1;
        }
        timer_wait_ticks(1);
    } while (timer_ticks() - start < TIMEOUT);
    h->control_slot = 0;
    /* Caller disables this slot before reclaiming or reusing any of its buffers. */
    return false;
}
static uint32_t *context(struct device *d, unsigned int index) {
    return d->input + index * (d->host->context_size / 4);
}
static void endpoint_context(uint32_t *ctx, const struct ring *ring, unsigned int type,
                             unsigned int packet, unsigned int interval) {
    ctx[0] = interval << 16;
    ctx[1] = (3u << 1) | type << 3 | packet << 16;
    ctx[2] = (uint32_t)ring->physical | 1;
    ctx[3] = (uint32_t)(ring->physical >> 32);
    ctx[4] = 8 | (type == 7 ? packet << 16 : 0);
}

static bool enumerate(struct host *h, struct device *d, unsigned int port) {
    d->host = h; d->port = port;
    const uintptr_t portsc = h->op + 0x400 + (port - 1) * 16;
    h->stage = "port-reset";
    uint32_t value = read32(portsc);
    if (!(value & 1)) { return false; }
    timer_wait_ticks(10); /* USB connect debounce. */
    value = read32(portsc);
    if (!(value & 1)) { return false; }
    write32(portsc, (value & PORT_KEEP) | PORT_CHANGES | (1u << 4));
    if (!wait_bits(portsc, (1u << 4) | 3, 3, TIMEOUT)) { return false; }
    timer_wait_ticks(2); /* Reset recovery, >=10 ms. */
    value = read32(portsc);
    d->speed = (value >> 10) & 15;
    write32(portsc, (value & PORT_KEEP) | PORT_CHANGES);
    if (d->speed < 1 || d->speed > 3) { ++stats.unsupported; return false; }
    h->stage = "enable-slot";
    if (!command(h, 0, TYPE(9) | (uint32_t)h->slot_type[port] << 16, &d->slot) ||
        !d->slot || d->slot > h->slots) { return false; }
    h->stage = "device-memory";
    d->input = dma_page(h, &d->input_phys);
    d->output = dma_page(h, &d->output_phys);
    d->data = dma_page(h, &d->data_phys);
    d->report = dma_page(h, &d->report_phys);
    if (!d->input || !d->output || !d->data || !d->report ||
        !ring_init(h, &d->control) || !ring_init(h, &d->interrupt)) { return false; }
    h->dcbaa[d->slot] = d->output_phys;
    d->input[1] = 3;
    context(d, 1)[0] = d->speed << 20 | 1u << 27;
    context(d, 1)[1] = port << 16;
    const unsigned int initial_packet = d->speed == 3 ? 64 : 8;
    endpoint_context(context(d, 2), &d->control, 4, initial_packet, 0);
    h->stage = "address-device";
    if (!command(h, d->input_phys, TYPE(11) | d->slot << 24, 0)) { return false; }
    h->stage = "device-descriptor";
    unsigned int actual;
    if (!control_in(d, 0x80, 6, 0x100, 0, 8, &actual) || actual != 8 ||
        d->data[0] != 18 || d->data[1] != 1) { return false; }
    const unsigned int packet = d->data[7];
    if ((d->speed == 2 && packet != 8) || (d->speed == 3 && packet != 64) ||
        (packet != 8 && packet != 16 && packet != 32 && packet != 64)) { return false; }
    if (packet != initial_packet) {
        zero(d->input, PMM_PAGE_SIZE);
        d->input[1] = 2;
        endpoint_context(context(d, 2), &d->control, 4, packet, 0);
        if (!command(h, d->input_phys, TYPE(13) | d->slot << 24, 0)) { return false; }
    }
    if (!control_in(d, 0x80, 6, 0x100, 0, 18, &actual) || actual != 18 ||
        d->data[0] != 18 || d->data[1] != 1 || !d->data[17]) { return false; }
    if (d->data[4] == 9) { h->stage = "hub-not-supported"; ++stats.unsupported; return false; }
    h->stage = "configuration";
    if (!control_in(d, 0x80, 6, 0x200, 0, 9, &actual) || actual != 9 ||
        d->data[0] != 9 || d->data[1] != 2) { return false; }
    const unsigned int size = d->data[2] | (unsigned int)d->data[3] << 8;
    if (size < 9 || size > PMM_PAGE_SIZE ||
        !control_in(d, 0x80, 6, 0x200, 0, (uint16_t)size, &actual) || actual != size) { return false; }
    struct usb_keyboard_config config;
    if (!usb_keyboard_config(d->data, size, &config)) {
        h->stage = "no-boot-keyboard"; ++stats.unsupported; return false;
    }
    if ((d->speed == 2 && config.packet > 8) || (d->speed == 1 && config.packet > 64) ||
        (d->speed == 3 && config.interval > 16)) { return false; }
    unsigned int interval = 0;
    if (d->speed == 3) { interval = config.interval - 1; }
    else {
        unsigned int microframes = (unsigned int)config.interval * 8;
        while ((2u << interval) <= microframes) { ++interval; }
    }
    d->endpoint = (config.endpoint & 15) * 2 + 1;
    zero(d->input, PMM_PAGE_SIZE);
    d->input[1] = 1 | (1u << d->endpoint);
    context(d, 1)[0] = d->speed << 20 | d->endpoint << 27;
    context(d, 1)[1] = port << 16;
    endpoint_context(context(d, d->endpoint + 1), &d->interrupt, 7, config.packet, interval);
    h->stage = "configure-endpoint";
    if (!command(h, d->input_phys, TYPE(12) | d->slot << 24, 0)) { return false; }
    h->stage = "set-configuration";
    if (!control_in(d, 0, 9, config.configuration, 0, 0, 0)) { return false; }
    h->stage = "set-boot-protocol";
    if (!control_in(d, 0x21, 11, 0, config.interface, 0, 0)) { return false; }
    /* SET_IDLE is optional for our polling path; repeated identical reports are deduplicated. */
    d->ready = true;
    ++stats.keyboards;
    for (unsigned int i = 0; i < REPORTS; ++i) { arm(d, i); }
    h->stage = "ready";
    return true;
}

static bool capabilities(struct host *h, uint32_t hcc) {
    unsigned int at = (hcc >> 16) * 4;
    for (unsigned int count = 0; at && count < 256; ++count) {
        if (at > MMIO_BYTES - 16) { return false; }
        const uint32_t cap = read32(h->base + at);
        if ((cap & 255) == 1) {
            h->stage = "firmware-handoff";
            /* OS-owned byte only; do not write the BIOS-owned semaphore. */
            *(volatile uint8_t *)(h->base + at + 3) = 1;
            if (!wait_bits(h->base + at, (1u << 24) | (1u << 16), 1u << 24, TIMEOUT)) { return false; }
            const uint32_t legacy = read32(h->base + at + 4);
            write32(h->base + at + 4, (legacy & 0x00001fee) | 0xe0000000u);
        } else if ((cap & 255) == 2) {
            const uint32_t ports = read32(h->base + at + 8);
            const unsigned int first = ports & 255, length = (ports >> 8) & 255;
            if (!first || !length || first + length - 1 > h->ports ||
                read32(h->base + at + 4) != 0x20425355) { return false; }
            const uint8_t type = (uint8_t)(read32(h->base + at + 12) & 31);
            for (unsigned int p = first; p < first + length; ++p) {
                if (h->protocol[p]) { return false; }
                h->protocol[p] = (uint8_t)(cap >> 24);
                h->slot_type[p] = type;
            }
        }
        const unsigned int next = ((cap >> 8) & 255) * 4;
        if (!next) { return true; }
        at += next;
    }
    return at == 0;
}

static bool start_host(struct host *h) {
    h->stage = "BAR";
    const uint32_t bar = pci_read(h->bdf, 0x10);
    if ((bar & 1) || ((bar >> 1) & 3) == 1 || ((bar >> 1) & 3) == 3) { return false; }
    uint64_t physical = bar & ~UINT64_C(15);
    if ((bar & 6) == 4) { physical |= (uint64_t)pci_read(h->bdf, 0x14) << 32; }
    if (!physical || physical % PMM_PAGE_SIZE || physical > UINT64_MAX - MMIO_BYTES) { return false; }
    h->stage = "MMIO";
    const uint64_t flags = arch_irq_save();
    while (h->mapped < MMIO_BYTES) {
        if (!vmm_map_mmio_page(h->base + h->mapped, physical + h->mapped)) { break; }
        h->mapped += PMM_PAGE_SIZE;
    }
    arch_irq_restore(flags);
    if (h->mapped != MMIO_BYTES) { return false; }
    h->original_command = (uint16_t)pci_read(h->bdf, 4);
    pci_command(h->bdf, h->original_command | 2);
    h->stage = "capabilities";
    const uint32_t version = read32(h->base), hcs = read32(h->base + 4), hcc = read32(h->base + 0x10);
    const unsigned int caplength = version & 255;
    h->ports = hcs >> 24;
    h->slots = (hcs & 255) < MAX_KEYS ? hcs & 255 : MAX_KEYS;
    h->context_size = (hcc & 4) ? 64 : 32;
    h->wide = (hcc & 1) != 0;
    const uint32_t db = read32(h->base + 0x14) & ~3u, rt = read32(h->base + 0x18) & ~31u;
    if (caplength < 0x20 || (caplength & 3) || (version >> 16) < 0x96 ||
        (version >> 16) > 0x120 || !h->ports || !h->slots || !((hcs >> 8) & 0x7ff) ||
        db < caplength + 0x400 + h->ports * 16 || db > MMIO_BYTES - (h->slots + 1) * 4 ||
        rt < caplength + 0x400 + h->ports * 16 || rt > MMIO_BYTES - 0x40) { return false; }
    h->op = h->base + caplength; h->doorbell = h->base + db; h->runtime = h->base + rt;
    if (!capabilities(h, hcc)) { return false; }
    h->owned = true;
    h->stage = "halt";
    write32(h->op, 0);
    if (!wait_bits(h->op + 4, 1, 1, TIMEOUT)) { return false; }
    h->stage = "reset";
    write32(h->op, 2);
    if (!wait_bits(h->op, 2, 0, TIMEOUT) || !wait_bits(h->op + 4, 1u << 11, 0, TIMEOUT) ||
        !(read32(h->op + 8) & 1)) { return false; }
    h->stage = "DMA-memory";
    h->dcbaa = dma_page(h, &h->dcbaa_phys);
    h->events = dma_page(h, &h->event_phys);
    uint64_t *erst = dma_page(h, &h->erst_phys);
    if (!h->dcbaa || !h->events || !erst || !ring_init(h, &h->command)) { return false; }
    const uint32_t hcs2 = read32(h->base + 8);
    const unsigned int scratchpads = ((hcs2 >> 27) & 31) | ((hcs2 >> 21) & 31) << 5;
    if (scratchpads > 64) { h->stage = "scratchpad-limit"; return false; }
    if (scratchpads) {
        uint64_t array_phys;
        uint64_t *array = dma_page(h, &array_phys);
        if (!array) { return false; }
        h->dcbaa[0] = array_phys;
        for (unsigned int i = 0; i < scratchpads; ++i) {
            if (!dma_page(h, &array[i])) { return false; }
        }
    }
    erst[0] = h->event_phys;
    erst[1] = TRBS;
    h->event_cycle = 1;
    write32(h->op + 0x38, h->slots);
    write64(h->op + 0x30, h->dcbaa_phys);
    write64(h->op + 0x18, h->command.physical | 1);
    write32(h->runtime + 0x20, 0); /* Interrupter 0 polled, IE=0. */
    write32(h->runtime + 0x28, 1);
    write64(h->runtime + 0x30, h->erst_phys);
    write64(h->runtime + 0x38, h->event_phys);
    pci_command(h->bdf, (h->original_command | 0x406) & (uint16_t)~1u);
    barrier();
    h->stage = "run";
    write32(h->op, 1); /* R/S; no legacy IRQ/MSI required. */
    if (!wait_bits(h->op + 4, 1, 0, TIMEOUT)) { return false; }
    h->live = true;
    h->stage = "command-ring";
    if (!command(h, 0, TYPE(23), 0)) { return false; }
    ++stats.controllers;
    h->counted = true;
    /* Firmware keeps ownership of routing on older Intel EHCI/xHCI chipsets. */
    for (unsigned int port = 1; port <= h->ports; ++port) {
        if (h->protocol[port] != 2) { continue; }
        const uintptr_t reg = h->op + 0x400 + (port - 1) * 16;
        const uint32_t value = read32(reg);
        if (!(value & (1u << 9))) { write32(reg, (value & PORT_KEEP) | (1u << 9)); }
    }
    timer_wait_ticks(20);
    unsigned int devices = 0;
    for (unsigned int port = 1; port <= h->ports && devices < MAX_KEYS && !h->failed; ++port) {
        if (h->protocol[port] != 2 || !(read32(h->op + 0x400 + (port - 1) * 16) & 1)) { continue; }
        struct device *d = &h->devices[devices];
        d->id = (unsigned int)(h - hosts) * MAX_KEYS + devices;
        const unsigned int checkpoint = h->page_count;
        if (enumerate(h, d, port)) { ++devices; }
        else {
            debug_write("[usb] Port "); debug_u64(port); debug_write(": "); debug_write(h->stage);
            debug_write(" (cmd="); debug_u64(h->command_code);
            debug_write(" transfer="); debug_u64(h->control_code); debug_write(")\n");
            if (d->slot && !h->failed && !command(h, 0, TYPE(10) | d->slot << 24, 0)) { host_fault(h); }
            if (!h->failed) {
                if (d->slot) { h->dcbaa[d->slot] = 0; }
                free_after(h, checkpoint);
                zero(d, sizeof(*d));
            }
        }
    }
    return !h->failed;
}

static void cleanup_failed(struct host *h) {
    bool safe = !h->owned;
    if (h->owned) {
        write32(h->op, 0);
        safe = wait_bits(h->op + 4, 1, 1, TIMEOUT);
        pci_command(h->bdf, (uint16_t)((pci_read(h->bdf, 4) | 0x400) & ~4u));
    } else if (h->mapped == MMIO_BYTES) { pci_command(h->bdf, h->original_command); }
    h->live = false;
    if (h->counted) { --stats.controllers; h->counted = false; }
    for (unsigned int i = 0; i < MAX_KEYS; ++i) { device_fault(&h->devices[i]); }
    if (!safe) { return; } /* Quarantine: hardware has not confirmed that DMA stopped. */
    free_after(h, 0);
    const uint64_t flags = arch_irq_save();
    while (h->mapped) { h->mapped -= PMM_PAGE_SIZE; vmm_unmap_mmio_page(h->base + h->mapped); }
    arch_irq_restore(flags);
}

bool usb_init(const struct boot_info *boot) {
    if (attempted || !boot || !vmm_root_physical() || !arch_irq_enabled()) { return false; }
    attempted = true;
    direct_map = boot->direct_map_offset;
    for (unsigned int bus = 0; bus < 256; ++bus) {
        for (unsigned int slot = 0; slot < 32; ++slot) {
            const uint16_t first = (uint16_t)(bus << 8 | slot << 3);
            if ((pci_read(first, 0) & 0xffff) == 0xffff) { continue; }
            const unsigned int functions = (pci_read(first, 12) & 0x800000) ? 8 : 1;
            for (unsigned int f = 0; f < functions; ++f) {
                const uint16_t bdf = first | (uint16_t)f;
                if ((pci_read(bdf, 0) & 0xffff) == 0xffff || (pci_read(bdf, 8) >> 16) != 0x0c03) { continue; }
                if (((pci_read(bdf, 8) >> 8) & 255) != 0x30 || host_count == MAX_HOSTS) {
                    ++stats.unsupported; continue;
                }
                struct host *h = &hosts[host_count++];
                h->bdf = bdf;
                h->base = VMM_MMIO_BASE + (host_count - 1) * MMIO_BYTES;
                debug_write("[usb] xHCI PCI "); debug_hex64(bdf); debug_write("\n");
                if (!start_host(h)) {
                    debug_write("[warn] xHCI initialization: "); debug_write(h->stage); debug_write("\n");
                    if (!h->failed) { ++stats.failed; h->failed = true; }
                    cleanup_failed(h);
                }
            }
        }
    }
    /* Enumeration may have received firmware-held keys: discard them before the prompt. */
    tail = head; lost = false;
    if (host_count || stats.unsupported) {
        debug_write("[usb] xHCI ready="); debug_u64(stats.controllers);
        debug_write(" keyboards="); debug_u64(stats.keyboards);
        debug_write(" unsupported="); debug_u64(stats.unsupported);
        debug_write(" failed="); debug_u64(stats.failed); debug_write("\n");
    }
    return stats.keyboards != 0;
}

enum keyboard_rx_result usb_read_key(uint8_t *value, unsigned int *device) {
    if (!value || !device) { return KEYBOARD_RX_EMPTY; }
    const uint64_t flags = arch_irq_save();
    for (unsigned int i = 0; i < host_count; ++i) { poll_host(&hosts[i]); }
    enum keyboard_rx_result result = KEYBOARD_RX_EMPTY;
    if (lost) { lost = false; *device = 0; result = KEYBOARD_RX_LOST; }
    else if (tail != head) {
        *value = queue[tail].key; *device = queue[tail].device;
        tail = (tail + 1) & (QUEUE_SIZE - 1);
        result = KEYBOARD_RX_BYTE;
    }
    arch_irq_restore(flags);
    return result;
}
bool usb_ready(void) { return stats.keyboards != 0; }
struct usb_stats usb_get_stats(void) {
    const uint64_t flags = arch_irq_save();
    struct usb_stats result = stats;
    result.queued = (head - tail) & (QUEUE_SIZE - 1);
    arch_irq_restore(flags);
    return result;
}
