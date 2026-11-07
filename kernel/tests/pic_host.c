#include <stdio.h>
#include <stdlib.h>
#include "../src/arch/x86_64/pic.h"

#define REQUIRE(test) do { \
    if (!(test)) { \
        fprintf(stderr, "PIC failure at line %d: %s\n", __LINE__, #test); \
        exit(1); \
    } \
} while (0)

struct write { uint16_t port; uint8_t value; };
static struct write writes[128];
static size_t count;
static uint8_t master_isr, slave_isr;

void outb(uint16_t port, uint8_t value) {
    REQUIRE(count < sizeof(writes) / sizeof(writes[0]));
    writes[count++] = (struct write){port, value};
}

uint8_t inb(uint16_t port) {
    REQUIRE(port == 0x20 || port == 0xa0);
    return port == 0x20 ? master_isr : slave_isr;
}

static void expect(size_t index, uint16_t port, uint8_t value) {
    REQUIRE(index < count && writes[index].port == port && writes[index].value == value);
}

int main(void) {
    REQUIRE(!pic_is_ready() && !pic_mask(0) && !pic_unmask(0) && !pic_begin_irq(0));
    pic_end_irq(0);
    REQUIRE(count == 0 && pic_init() && pic_is_ready());
    const struct write init[] = {
        {0x21, 0xff}, {0xa1, 0xff}, {0x20, 0x11}, {0xa0, 0x11},
        {0x21, 32}, {0xa1, 40}, {0x21, 4}, {0xa1, 2},
        {0x21, 1}, {0xa1, 1}, {0x21, 0xff}, {0xa1, 0xff},
    };
    REQUIRE(count == 2 * sizeof(init) / sizeof(init[0]));
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
        expect(i * 2, init[i].port, init[i].value);
        expect(i * 2 + 1, 0x80, 0);
    }
    count = 0;
    REQUIRE(!pic_init() && !pic_mask(2) && !pic_unmask(2));
    REQUIRE(!pic_mask(16) && !pic_unmask(255) && !pic_begin_irq(16));
    pic_end_irq(16);
    REQUIRE(count == 0);
    REQUIRE(pic_unmask(0) && pic_unmask(8) && pic_unmask(15));
    REQUIRE(pic_mask(8) && pic_mask(15) && pic_mask(0));
    const struct write masks[] = {
        {0x21, 0xfe}, {0xa1, 0xfe}, {0x21, 0xfa}, {0xa1, 0x7e}, {0x21, 0xfa},
        {0xa1, 0x7f}, {0xa1, 0xff}, {0x21, 0xfe}, {0x21, 0xff},
    };
    REQUIRE(count == sizeof(masks) / sizeof(masks[0]));
    for (size_t i = 0; i < count; ++i) {
        expect(i, masks[i].port, masks[i].value);
    }
    count = 0;
    REQUIRE(!pic_begin_irq(7));
    REQUIRE(count == 1);
    expect(0, 0x20, 0x0b);
    count = 0;
    REQUIRE(!pic_begin_irq(15));
    REQUIRE(count == 2);
    expect(0, 0xa0, 0x0b);
    expect(1, 0x20, 0x20);
    master_isr = slave_isr = 0x80;
    count = 0;
    REQUIRE(pic_begin_irq(7));
    pic_end_irq(7);
    REQUIRE(count == 2);
    expect(0, 0x20, 0x0b);
    expect(1, 0x20, 0x20);
    count = 0;
    REQUIRE(pic_begin_irq(15));
    pic_end_irq(15);
    REQUIRE(count == 3);
    expect(0, 0xa0, 0x0b);
    expect(1, 0xa0, 0x20);
    expect(2, 0x20, 0x20);
    for (uint8_t irq = 0; irq < 16; ++irq) {
        if (irq == 7 || irq == 15) {
            continue;
        }
        count = 0;
        REQUIRE(pic_begin_irq(irq) && count == 0);
        pic_end_irq(irq);
        REQUIRE(count == (irq >= 8 ? 2u : 1u));
        if (irq >= 8) {
            expect(0, 0xa0, 0x20);
        }
        expect(count - 1, 0x20, 0x20);
    }
    puts("PASS: PIC initialization, masks/cascade, spurious IRQs and EOI order.");
    return 0;
}
