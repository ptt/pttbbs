#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include "cmsys.h"

/* =========================================================================
 * 1. Base32 Decoding and Encoding (RFC 4648)
 * ========================================================================= */
static
int base32_decode(const char *encoded, uint8_t *output, size_t max_out_len) {
    if (!encoded || !output) return -1;
    int buffer = 0;
    int bits_left = 0;
    size_t out_len = 0;

    for (const char *ptr = encoded; *ptr; ptr++) {
        char ch = *ptr;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '-')
            continue;
        if (ch == '=')
            break;

        int val;
        if (ch >= 'A' && ch <= 'Z') val = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') val = ch - 'a';
        else if (ch >= '2' && ch <= '7') val = ch - '2' + 26;
        else return -1;

        buffer = (buffer << 5) | val;
        bits_left += 5;

        if (bits_left >= 8) {
            if (out_len >= max_out_len) return -1;
            output[out_len++] = (uint8_t)(buffer >> (bits_left - 8));
            bits_left -= 8;
        }
    }
    return (int)out_len;
}

static
int base32_encode(const uint8_t *data, size_t length, char *output, size_t max_out_len) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    if (!data || !output) return -1;
    size_t out_len = 0;
    int buffer = 0;
    int bits_left = 0;

    for (size_t i = 0; i < length; i++) {
        buffer = (buffer << 8) | data[i];
        bits_left += 8;
        while (bits_left >= 5) {
            if (out_len + 1 >= max_out_len) return -1;
            output[out_len++] = alphabet[(buffer >> (bits_left - 5)) & 31];
            bits_left -= 5;
        }
    }
    if (bits_left > 0) {
        if (out_len + 1 >= max_out_len) return -1;
        output[out_len++] = alphabet[(buffer << (5 - bits_left)) & 31];
    }
    if (out_len < max_out_len) {
        output[out_len] = '\0';
    }
    return (int)out_len;
}

/* =========================================================================
 * 2. SHA-1 & HMAC-SHA1 Implementation (RFC 3174 / RFC 2104)
 * ========================================================================= */

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} SHA1_CTX;

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i * 4] << 24) | ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) | ((uint32_t)buffer[i * 4 + 3]);
    }
    for (i = 16; i < 80; i++) {
        uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = (val << 1) | (val >> 31);
    }
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

static void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i, j;
    j = (size_t)((ctx->count >> 3) & 63);
    ctx->count += (uint64_t)len << 3;
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64)
            sha1_transform(ctx->state, &data[i]);
        j = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(SHA1_CTX *ctx, uint8_t digest[20]) {
    uint8_t finalcount[8];
    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count >> ((7 - i) * 8)) & 0xFF);
    }
    uint8_t c = 0x80;
    sha1_update(ctx, &c, 1);
    while ((ctx->count & 504) != 448) {
        c = 0x00;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 0xFF);
    }
}

static
void hmac_sha1(const uint8_t *key, size_t key_len,
               const uint8_t *msg, size_t msg_len,
               uint8_t output[20])
{
    uint8_t k0[64];
    memset(k0, 0, sizeof(k0));

    if (key_len > 64) {
        SHA1_CTX ctx;
        sha1_init(&ctx);
        sha1_update(&ctx, key, key_len);
        sha1_final(&ctx, k0);
    } else {
        memcpy(k0, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5C;
    }

    uint8_t inner_digest[20];
    SHA1_CTX inner_ctx;
    sha1_init(&inner_ctx);
    sha1_update(&inner_ctx, ipad, 64);
    sha1_update(&inner_ctx, msg, msg_len);
    sha1_final(&inner_ctx, inner_digest);

    SHA1_CTX outer_ctx;
    sha1_init(&outer_ctx);
    sha1_update(&outer_ctx, opad, 64);
    sha1_update(&outer_ctx, inner_digest, 20);
    sha1_final(&outer_ctx, output);
}

/* =========================================================================
 * 3. 2FA Code Generation & Verification (RFC 6238)
 * ========================================================================= */

int generate_2fa(const uint8_t *secret, size_t secret_len, uint64_t timestamp, uint32_t step_seconds, char code_out[7]) {
    uint64_t counter = timestamp / step_seconds;
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)(counter & 0xFF);
        counter >>= 8;
    }

    uint8_t hmac[20];
    hmac_sha1(secret, secret_len, msg, 8, hmac);

    int offset = hmac[19] & 0x0F;
    uint32_t truncated_hash = ((uint32_t)(hmac[offset] & 0x7F) << 24) |
                              ((uint32_t)(hmac[offset + 1] & 0xFF) << 16) |
                              ((uint32_t)(hmac[offset + 2] & 0xFF) << 8) |
                              ((uint32_t)(hmac[offset + 3] & 0xFF));

    uint32_t code = truncated_hash % 1000000;
    snprintf(code_out, 7, "%06u", code);
    return 0;
}

void generate_2fa_secret(char secret_base32[17]) {
    uint8_t random_bytes[10];
    arc4random_buf(random_bytes, sizeof(random_bytes));
    base32_encode(random_bytes, sizeof(random_bytes), secret_base32, 17);
}

int verify_2fa(const char *secret_base32, const char *user_code, int time_window) {
    if (!secret_base32 || !user_code || strlen(user_code) != 6)
        return 0;

    uint8_t secret[64];
    int secret_len = base32_decode(secret_base32, secret, sizeof(secret));
    if (secret_len <= 0)
        return 0;

    uint64_t now = (uint64_t)time(NULL);
    char expected_code[7];

    for (int i = -time_window; i <= time_window; i++) {
        uint64_t ts = (uint64_t)((int64_t)now + i * 30);
        generate_2fa(secret, (size_t)secret_len, ts, 30, expected_code);
        if (strcmp(expected_code, user_code) == 0) {
            return 1;
        }
    }
    return 0;
}

void generate_backup_code(char code_out[9]) {
    uint32_t val = arc4random_uniform(100000000);
    snprintf(code_out, 9, "%08u", val);
}

/* =========================================================================
 * 4. Clean 2FA Prompt Renderer
 * =========================================================================
 * I tried hard to generate the QR code (qrencode) but it really can't fit
 * 80x24 screen - need 29 lines. The UTF8 version works better in ~15 lines,
 * but it doesn't work on Big5 terminals.
 * So in the end the account name and issuer are useless.
 */
