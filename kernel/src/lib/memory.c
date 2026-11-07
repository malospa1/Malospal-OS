#include <stddef.h>
#include <stdint.h>

void *memcpy(void *restrict dest, const void *restrict source, size_t size) {
    unsigned char *out = dest;
    const unsigned char *in = source;
    for (size_t i = 0; i < size; ++i) {
        out[i] = in[i];
    }
    return dest;
}

void *memmove(void *dest, const void *source, size_t size) {
    unsigned char *out = dest;
    const unsigned char *in = source;
    if ((uintptr_t)out <= (uintptr_t)in) {
        for (size_t i = 0; i < size; ++i) {
            out[i] = in[i];
        }
    } else {
        while (size != 0) {
            --size;
            out[size] = in[size];
        }
    }
    return dest;
}

void *memset(void *dest, int value, size_t size) {
    unsigned char *out = dest;
    for (size_t i = 0; i < size; ++i) {
        out[i] = (unsigned char)value;
    }
    return dest;
}

int memcmp(const void *first, const void *second, size_t size) {
    const unsigned char *a = first;
    const unsigned char *b = second;
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}
