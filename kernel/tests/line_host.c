#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/line.h"

#define REQUIRE(test) do { \
    if (!(test)) { \
        fprintf(stderr, "Line failure at %d: %s\n", __LINE__, #test); \
        exit(1); \
    } \
} while (0)

static void text(struct console_line *line, const char *value) {
    while (*value) {
        (void)line_feed(line, (uint8_t)*value++);
    }
}

int main(void) {
    struct console_line line = {0};
    REQUIRE(line_feed(&line, '\b') == LINE_NONE);
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && !line.text[0]);
    text(&line, "helx\bp");
    REQUIRE(line_feed(&line, '\r') == LINE_SUBMIT && !strcmp(line.text, "help"));
    REQUIRE(line_feed(&line, '\n') == LINE_NONE);
    text(&line, "memx\x7f");
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && !strcmp(line.text, "mem"));
    text(&line, "\techo\tx");
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && !strcmp(line.text, " echo x"));
    text(&line, "halt");
    REQUIRE(line_feed(&line, 0x15) == LINE_CLEAR && line.length == 0);
    text(&line, "info");
    REQUIRE(line_feed(&line, '\r') == LINE_SUBMIT && !strcmp(line.text, "info"));
    text(&line, "ticks");
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && !strcmp(line.text, "ticks"));
    for (unsigned int i = 0; i < CONSOLE_LINE_CAPACITY - 1; ++i) {
        REQUIRE(line_feed(&line, 'x') == LINE_ECHO);
    }
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && strlen(line.text) == 127);
    for (unsigned int i = 0; i < CONSOLE_LINE_CAPACITY; ++i) {
        (void)line_feed(&line, 'x');
    }
    text(&line, "\b\x7fhalt");
    REQUIRE(line_feed(&line, '\r') == LINE_REJECT);
    REQUIRE(line_feed(&line, '\n') == LINE_NONE);
    for (unsigned int value = 0; value <= 255; ++value) {
        if ((value >= 32 && value <= 126) || value == 8 || value == 9 ||
            value == 10 || value == 13 || value == 21 || value == 127) {
            continue;
        }
        text(&line, "ha");
        REQUIRE(line_feed(&line, (uint8_t)value) == LINE_NONE);
        text(&line, "lt");
        REQUIRE(line_feed(&line, '\n') == LINE_REJECT);
    }
    text(&line, "ha");
    line_invalidate(&line);
    text(&line, "lt");
    REQUIRE(line_feed(&line, '\n') == LINE_REJECT);
    line_invalidate(&line);
    REQUIRE(line_feed(&line, 21) == LINE_CLEAR);
    text(&line, "echo recovered");
    REQUIRE(line_feed(&line, '\n') == LINE_SUBMIT && !strcmp(line.text, "echo recovered"));
    uint32_t random = 1;
    for (unsigned int i = 0; i < 100000; ++i) {
        random = random * 1664525u + 1013904223u;
        const enum line_result result = line_feed(&line, (uint8_t)(random >> 24));
        REQUIRE(line.length < CONSOLE_LINE_CAPACITY);
        if (result == LINE_SUBMIT || result == LINE_REJECT) {
            REQUIRE(memchr(line.text, 0, CONSOLE_LINE_CAPACITY) != NULL);
        }
    }
    puts("PASS: Console line editing, CR/LF, bounds, invalid input and recovery.");
    return 0;
}
