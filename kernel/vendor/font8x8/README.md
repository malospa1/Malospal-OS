# font8x8

Public Domain bitmap font by Daniel Hepper, based on Marcel Sondaar's public
domain VGA font. The original attribution is preserved in font8x8_basic.h.

Source: https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h
Downloaded 2026-10-15. Original SHA-256:
49d8df366296b203ca3211bc0672cf2a762135bf12710735b6292756b19dffd5

Local change: the glyph table is static const unsigned char so it lives in
read-only kernel memory. The renderer doubles the eight rows to 8x16 cells.
