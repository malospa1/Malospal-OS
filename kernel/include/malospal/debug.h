#ifndef MALOSPAL_DEBUG_H
#define MALOSPAL_DEBUG_H

#include <stdbool.h>
#include <stdint.h>

/* Early COM1 setup; false does not prevent later screen-only output. */
bool debug_init(void);
/* Output goes to every available sink: COM1 and the initialized screen. */
void debug_write(const char *text);
void debug_u64(uint64_t value);
void debug_hex64(uint64_t value);

#endif
