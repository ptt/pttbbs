#define _UTIL_C_
#include "bbs.h"
#include "sgr66.h"

extern const uint16_t b2u_table[];

static size_t
parse_sgr(const uint8_t *buf, size_t len, size_t pos)
{
    if (pos + 2 >= len || buf[pos] != 0x1b || buf[pos + 1] != '[')
        return 0;
    size_t k = pos + 2;
    while (k < len && ((buf[k] >= '0' && buf[k] <= '9') || buf[k] == ';'))
        k++;
    if (k < len && buf[k] == 'm')
        return (k - pos) + 1;
    return 0;
}

static inline int
is_valid_big5_uao(uint8_t hi, uint8_t lo)
{
    uint16_t b5 = ((uint16_t)hi << 8) | (uint16_t)lo;
    return b2u_table[b5] != b5;
}

int
scan_big5_uao(const uint8_t *buf, size_t len,
              int *out_has_dbcs, int *out_has_split_sgr)
{
    int has_dbcs = 0;
    int has_split = 0;
    size_t i = 0;

    if (out_has_dbcs)
        *out_has_dbcs = 0;
    if (out_has_split_sgr)
        *out_has_split_sgr = 0;

    while (i < len) {
        if (buf[i] < 0x80) {
            i++;
            continue;
        }

        uint8_t hi = buf[i];
        size_t j = i + 1;
        size_t sgr_count = 0;
        while (j < len) {
            size_t sgr_len = parse_sgr(buf, len, j);
            if (sgr_len == 0)
                break;
            j += sgr_len;
            sgr_count++;
        }

        if (j >= len)
            return 0;

        uint8_t lo = buf[j];
        if (!is_valid_big5_uao(hi, lo))
            return 0;

        has_dbcs = 1;
        if (sgr_count > 0)
            has_split = 1;

        i = j + 1;
    }

    if (!has_dbcs)
        return 0;

    if (is_valid_utf8(buf, len))
        return 0;

    if (out_has_dbcs)
        *out_has_dbcs = has_dbcs;
    if (out_has_split_sgr)
        *out_has_split_sgr = has_split;
    return 1;
}

int
convert_big5_sgr66(const uint8_t *buf, size_t len,
                   int to_utf8, bytebuf_t *out)
{
    int prev_was_sgr = 0;
    size_t i = 0;

    while (i < len) {
        size_t sgr_len = parse_sgr(buf, len, i);
        if (sgr_len > 0) {
            if (buf_append(out, buf + i, sgr_len) < 0)
                return -1;
            prev_was_sgr = 1;
            i += sgr_len;
            continue;
        }

        if (buf[i] < 0x80) {
            if (buf_append(out, buf + i, 1) < 0)
                return -1;
            prev_was_sgr = 0;
            i++;
            continue;
        }

        uint8_t hi = buf[i];
        size_t j = i + 1;
        size_t sgr_count = 0;
        while (j < len) {
            size_t mid_sgr_len = parse_sgr(buf, len, j);
            if (mid_sgr_len == 0)
                break;
            j += mid_sgr_len;
            sgr_count++;
        }

        if (j >= len)
            return -1;

        uint8_t lo = buf[j];
        if (sgr_count > 0) {
            if (prev_was_sgr && out->len > 0 && out->data[out->len - 1] == 'm') {
                out->len--;
                if (buf_append(out, ";66", 3) < 0)
                    return -1;
            } else {
                if (buf_append(out, "\x1b[66", 4) < 0)
                    return -1;
            }

            size_t k = i + 1;
            while (k < j) {
                size_t mid_sgr_len = parse_sgr(buf, len, k);
                if (buf_append(out, ";", 1) < 0)
                    return -1;
                if (buf_append(out, buf + k + 2, mid_sgr_len - 3) < 0)
                    return -1;
                k += mid_sgr_len;
            }

            if (buf_append(out, "m", 1) < 0)
                return -1;
        }

        if (to_utf8) {
            uint16_t b5 = ((uint16_t)hi << 8) | (uint16_t)lo;
            uint8_t utf8[4];
            int ulen = ucs2utf(b2u_table[b5], utf8);
            if (buf_append(out, utf8, (size_t)ulen) < 0)
                return -1;
        } else {
            uint8_t pair[2] = { hi, lo };
            if (buf_append(out, pair, 2) < 0)
                return -1;
        }

        prev_was_sgr = 0;
        i = j + 1;
    }

    return 0;
}
