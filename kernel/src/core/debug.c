#include <malospal/debug.h>
#include <malospal/screen.h>
#include <malospal/serial.h>

bool debug_init(void) { return serial_init(); }

static void write_byte(char value) {
    if (value == '\n') { serial_write_byte('\r'); }
    serial_write_byte(value);
    screen_putc(value);
}

void debug_write(const char *text) {
    for (; *text != '\0'; ++text) { write_byte(*text); }
}

void debug_u64(uint64_t value) {
    char digits[20];
    unsigned int count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) { write_byte(digits[--count]); }
}

void debug_hex64(uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    debug_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) { write_byte(digits[(value >> shift) & 0xf]); }
}
