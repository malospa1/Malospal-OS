#ifndef MALOSPAL_CONSOLE_LINE_H
#define MALOSPAL_CONSOLE_LINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONSOLE_LINE_CAPACITY 128u

enum line_result { LINE_NONE, LINE_ECHO, LINE_ERASE, LINE_CLEAR, LINE_SUBMIT, LINE_REJECT };
struct console_line {
    char text[CONSOLE_LINE_CAPACITY];
    size_t length;
    bool rejected, skip_lf;
};

/* A submitted string stays valid until the next feed. Zero-initialize state. */
enum line_result line_feed(struct console_line *line, uint8_t value);
void line_invalidate(struct console_line *line);

#endif
