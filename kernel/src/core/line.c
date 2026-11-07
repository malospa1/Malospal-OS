#include "line.h"

void line_invalidate(struct console_line *line) {
    line->rejected = true;
    line->skip_lf = false;
}

enum line_result line_feed(struct console_line *line, uint8_t value) {
    const bool skip = line->skip_lf && value == '\n';
    line->skip_lf = false;
    if (skip) {
        return LINE_NONE;
    }
    if (value == '\r' || value == '\n') {
        line->skip_lf = value == '\r';
        line->text[line->length] = '\0';
        const bool rejected = line->rejected;
        line->length = 0;
        line->rejected = false;
        return rejected ? LINE_REJECT : LINE_SUBMIT;
    }
    if (value == 0x15) { /* Ctrl+U explicitly cancels the whole line. */
        line->length = 0;
        line->rejected = false;
        return LINE_CLEAR;
    }
    if (line->rejected) {
        return LINE_NONE;
    }
    if (value == '\b' || value == 0x7f) {
        if (line->length == 0) {
            return LINE_NONE;
        }
        --line->length;
        return LINE_ERASE;
    }
    if (value == '\t') {
        value = ' ';
    }
    if (value < ' ' || value > '~' || line->length == CONSOLE_LINE_CAPACITY - 1) {
        line_invalidate(line);
        return LINE_NONE;
    }
    line->text[line->length++] = (char)value;
    return LINE_ECHO;
}
