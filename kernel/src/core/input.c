#include <malospal/input.h>
#include <malospal/usb.h>

enum serial_rx_result input_read(uint8_t *value, unsigned int *source, bool arrows) {
    static unsigned int next;
    for (unsigned int i = 0; i < 3; ++i) {
        const unsigned int which = (next + i) % 3;
        enum serial_rx_result result;
        unsigned int device = 0;
        if (which == 0) { result = serial_read(value); }
        else {
            enum keyboard_rx_result k;
            do {
                k = which == 1 ? keyboard_read_key(value) : usb_read_key(value, &device);
            } while (!arrows && k == KEYBOARD_RX_BYTE && *value >= KEY_UP);
            result = k == KEYBOARD_RX_BYTE ? SERIAL_RX_BYTE :
                     k == KEYBOARD_RX_LOST ? SERIAL_RX_LOST : SERIAL_RX_EMPTY;
        }
        if (result != SERIAL_RX_EMPTY) {
            *source = which == 0 ? INPUT_SERIAL : which == 1 ? INPUT_KEYBOARD : INPUT_USB + device;
            if (which == 0 && arrows && result == SERIAL_RX_BYTE && *value >= KEY_UP) { *value = 0; }
            next = (which + 1) % 3;
            return result;
        }
    }
    return SERIAL_RX_EMPTY;
}
bool input_ready(void) { return serial_rx_ready() || keyboard_get_stats().ready || usb_ready(); }
