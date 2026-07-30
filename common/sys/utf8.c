#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

int ucs2utf(uint16_t ucs2, uint8_t *utf8) {
    // assume utf8 has enough space.

    // (1) 0xxxxxxx -> 0xxxxxxx
    if ((ucs2 & (~0x7F)) == 0) {
        *utf8 = ucs2;
        return 1;
    } 

    if ((ucs2 & 0xF800) == 0) {
        // (2) 00000yyy yyxxxxxx -> 110yyyyy 10xxxxxx
        *utf8++ = 0xC0 | (ucs2 >> 6);
        *utf8++ = 0x80 | (ucs2 & 0x3F);
        return 2;
    } else {
        // (3) zzzzyyyy yyxxxxxx -> 1110zzzz 10yyyyyy 10xxxxxx
        *utf8++ = 0xE0 | (ucs2 >> 12);
        *utf8++ = 0x80 | ((ucs2 >> 6) & 0x3F);
        *utf8++ = 0x80 | ((ucs2) & 0x3F);
        return 3;
    }
}

int utf2ucs(const uint8_t *utf8, uint16_t *pucs) {
    uint16_t c;
    c = *utf8++;
    if ((c & 0x80) == 0) {
        *pucs = c;
        return 1;
    }
    switch (c >> 5) {
        case 0x06:
            // case 2
            *pucs = ((c & 0x1F) << 6) | (utf8[0] & 0x3F);
            return 2;

        case 0x07:
            // case 3
            *pucs = ((c & 0x0F) << 12) |
                    ((utf8[0] & 0x3F) << 6) |
                    (utf8[1] & 0x3F);
            return 3;
    }
    // unknown character
    *pucs = '?';
    return 1;
}

extern const uint16_t u2b_table[];

void utf8_to_big5(const char *utf8, char *big5, size_t max_len) {
    if (!utf8 || !big5 || max_len == 0) {
        return;
    }
    const uint8_t *p = (const uint8_t *)utf8;
    size_t out_idx = 0;
    while (*p && out_idx < (max_len - 1)) {
        uint16_t ucs2 = 0;
        int step = utf2ucs(p, &ucs2);
        p += step;
        uint16_t b5 = u2b_table[ucs2];
        if (b5 == 0) {
            b5 = '?';
        }
        if (b5 > 0xFF) {
            if (out_idx + 2 >= max_len) {
                break;
            }
            big5[out_idx++] = (char)(b5 >> 8);
            big5[out_idx++] = (char)(b5 & 0xFF);
        } else {
            big5[out_idx++] = (char)b5;
        }
    }
    big5[out_idx] = '\0';
}

extern const uint16_t b2u_table[];

void big5_to_utf8(const char *big5, char *utf8, size_t max_len) {
    if (!big5 || !utf8 || max_len == 0) {
        return;
    }
    const uint8_t *p = (const uint8_t *)big5;
    size_t out_idx = 0;
    while (*p && out_idx < (max_len - 1)) {
        uint16_t b5 = *p;
        uint16_t ucs2 = 0;
        if (b5 & 0x80) {
            if (p[1] != '\0') {
                uint16_t b5_full = ((uint16_t)p[0] << 8) | (uint16_t)p[1];
                p += 2;
                ucs2 = b2u_table[b5_full];
            } else {
                p++;
                ucs2 = '?';
            }
        } else {
            p++;
            ucs2 = b5;
        }

        uint8_t utf_buf[4] = {0};
        int utf_len = ucs2utf(ucs2, utf_buf);
        if (out_idx + (size_t)utf_len >= max_len) {
            break;
        }
        for (int i = 0; i < utf_len; i++) {
            utf8[out_idx++] = (char)utf_buf[i];
        }
    }
    utf8[out_idx] = '\0';
}

#ifdef _TEST_MAIN_

const char * print_bits(uint8_t c) {
    static char bits[9] = {0};
    int i;
    for (i = 0; i < 8; i++)
        bits[i] = ((c >> (7-i)) & 0x01) ? '1' : '0';
    return bits;
}

void print_bytes(uint8_t *bytes, int len) {
    while (len-- > 0)
        printf("%02X ", *bytes++);
}

void wikipedia_test() {
    const int tests = 3;
    uint16_t ucs[] = {
        0x24, 0xA2, 0x20AC, 0
    };
    uint8_t utf[][4] = {
        {0x24, 0},
        {0xC2, 0xA2, 0},
        {0xE2, 0x82, 0xAC, 0},
    };
    uint8_t t[4];
    int len, i;

    for (i = 0; i < tests; i++) {
        uint16_t got;
        utf2ucs(utf[i], &got);
        if (got != ucs[i]) {
            printf("wikipedia_test utf2ucs: failed in %04X (got %04X)\n", ucs[i], got);
        } else {
            printf("wikipedia_test utf2ucs: passed %04X\n", ucs[i]);
        }
        memset(t, 0, sizeof(t));
        len = ucs2utf(ucs[i], t);
        if (strcmp(t, utf[i]) != 0) {
            printf("wikipedia_test ucs2utf: failed in %04X (got %d)\n", ucs[i], len);
            print_bytes(utf[i], 4);
            printf("\n");
            print_bytes(t, len);
            printf("\n");
        }  else {
            printf("wikipedia_test ucs2utf: passed %04X\n", ucs[i]);
        }
    }
}

int main(int argc, char *argv[]) {
    wikipedia_test();
    return 0;
}
#endif
