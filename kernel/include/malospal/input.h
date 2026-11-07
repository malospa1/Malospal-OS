#ifndef MALOSPAL_INPUT_H
#define MALOSPAL_INPUT_H
#include <malospal/keyboard.h>
#include <malospal/serial.h>
enum input_source { INPUT_NONE, INPUT_SERIAL, INPUT_KEYBOARD, INPUT_USB };
/* USB sources occupy INPUT_USB + device ID. Fair polling; ASCII or KEY_* arrows. */
enum serial_rx_result input_read(uint8_t *value, unsigned int *source, bool arrows);
bool input_ready(void);
#endif
