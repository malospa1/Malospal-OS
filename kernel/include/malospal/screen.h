#ifndef MALOSPAL_SCREEN_H
#define MALOSPAL_SCREEN_H

#include <malospal/boot.h>

/* Early boot only. The caller provides a validated, mapped framebuffer. */
bool screen_init(const struct boot_framebuffer *fb);
bool screen_ready(void);
/* Single CPU; preserves IF. Reentrant output is skipped, never blocks. */
void screen_putc(char value);

struct screen_size { unsigned int width, height; };
struct screen_size screen_size(void);
/* Single foreground client; drawing is clipped and preserves IF. Text output
 * restores the console when the drawing lock is free; COM1 remains the fallback. */
bool screen_graphics_begin(void);
void screen_graphics_end(void);
void screen_fill_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height, uint32_t rgb);
void screen_draw_text(unsigned int x, unsigned int y, const char *text, unsigned int scale, uint32_t rgb);

#endif
