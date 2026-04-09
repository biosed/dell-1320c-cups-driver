#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sq21_simple.h"

enum {
    SQ21_MARGIN = 0x80,
    SQ21_EXTRA = 0x8c,
    SQ21_PREDICTORS = 5,
    SQ21_NTABLE_SIZE = 0x40,
    SQ21_RUN_CHUNK = 0x42a1,
};

typedef struct {
    int id;
    int offset;
} sq21_pred_t;

typedef struct {
    int src_width;
    int width;
    int height;
    int dpi;
    int line;
    int zoffset;
    uint8_t outpix;

    int hid;
    int run;
    int code;
    int shift;

    uint8_t ntable[SQ21_NTABLE_SIZE];

    uint32_t *pixbuf;
    int32_t *errbuf;
    uint32_t *pix[2];
    int32_t *err[2];
    uint8_t *pix8buf;
    int8_t *err8buf;
    uint8_t *pix8[2];
    int8_t *err8[2];
    int line_stride;

    sq21_pred_t pred[SQ21_PREDICTORS];
    sq21_pred_t *order[SQ21_PREDICTORS];
    sq21_pred_t weight[4];

    uint8_t *out;
    size_t out_size;
    size_t out_cap;
    uint32_t bitbuf;
    int bitbuf_len;
} sq21_t;

static const uint32_t SQ21_HPE0_CODE[256] = {
    0x000, 0x001, 0x00c, 0x00d, 0x020, 0x021, 0x022, 0x023,
    0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x056, 0x057,
    0x180, 0x181, 0x182, 0x183, 0x184, 0x185, 0x186, 0x187,
    0x188, 0x189, 0x18a, 0x18b, 0x18c, 0x18d, 0x18e, 0x18f,
    0x190, 0x191, 0x192, 0x193, 0x194, 0x195, 0x196, 0x197,
    0x198, 0x199, 0x19a, 0x19b, 0x19c, 0x19d, 0x19e, 0x19f,
    0x380, 0x381, 0x382, 0x383, 0x384, 0x385, 0x386, 0x387,
    0x388, 0x389, 0x38a, 0x38b, 0x38c, 0x38d, 0x38e, 0x38f,
    0x390, 0x391, 0x392, 0x393, 0x394, 0x395, 0x396, 0x397,
    0x398, 0x399, 0x39a, 0x39b, 0x39c, 0x39d, 0x39e, 0x39f,
    0x780, 0x781, 0x782, 0x783, 0x784, 0x785, 0x786, 0x787,
    0x788, 0x789, 0x78a, 0x78b, 0x78c, 0x78d, 0x78e, 0x78f,
    0x790, 0x791, 0x792, 0x793, 0x794, 0x795, 0x796, 0x797,
    0x798, 0x799, 0x79a, 0x79b, 0x79c, 0x79d, 0x79e, 0x79f,
    0x7a0, 0x7a1, 0x7a2, 0x7a3, 0x7a4, 0x7a5, 0x7a6, 0x7a7,
    0x7a8, 0x7a9, 0x7aa, 0x7ab, 0x7ac, 0x7ad, 0x7ae, 0x7af,
    0x7f0, 0x7ef, 0x7ee, 0x7ed, 0x7ec, 0x7eb, 0x7ea, 0x7e9,
    0x7e8, 0x7e7, 0x7e6, 0x7e5, 0x7e4, 0x7e3, 0x7e2, 0x7e1,
    0x7e0, 0x7df, 0x7de, 0x7dd, 0x7dc, 0x7db, 0x7da, 0x7d9,
    0x7d8, 0x7d7, 0x7d6, 0x7d5, 0x7d4, 0x7d3, 0x7d2, 0x7d1,
    0x7d0, 0x7cf, 0x7ce, 0x7cd, 0x7cc, 0x7cb, 0x7ca, 0x7c9,
    0x7c8, 0x7c7, 0x7c6, 0x7c5, 0x7c4, 0x7c3, 0x7c2, 0x7c1,
    0x7c0, 0x3bf, 0x3be, 0x3bd, 0x3bc, 0x3bb, 0x3ba, 0x3b9,
    0x3b8, 0x3b7, 0x3b6, 0x3b5, 0x3b4, 0x3b3, 0x3b2, 0x3b1,
    0x3b0, 0x3af, 0x3ae, 0x3ad, 0x3ac, 0x3ab, 0x3aa, 0x3a9,
    0x3a8, 0x3a7, 0x3a6, 0x3a5, 0x3a4, 0x3a3, 0x3a2, 0x3a1,
    0x3a0, 0x1bf, 0x1be, 0x1bd, 0x1bc, 0x1bb, 0x1ba, 0x1b9,
    0x1b8, 0x1b7, 0x1b6, 0x1b5, 0x1b4, 0x1b3, 0x1b2, 0x1b1,
    0x1b0, 0x1af, 0x1ae, 0x1ad, 0x1ac, 0x1ab, 0x1aa, 0x1a9,
    0x1a8, 0x1a7, 0x1a6, 0x1a5, 0x1a4, 0x1a3, 0x1a2, 0x1a1,
    0x1a0, 0x05f, 0x05e, 0x05d, 0x05c, 0x05b, 0x05a, 0x059,
    0x058, 0x027, 0x026, 0x025, 0x024, 0x00f, 0x00e, 0x002,
};

static const uint8_t SQ21_HPE0_BITS[256] = {
     3,  3,  5,  5,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  7,  7,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  5,  5,  3,
};

static const uint32_t SQ21_HID[2][7][2] = {
    {
        {0x0, 1}, {0x6, 3}, {0x1c, 5}, {0x1d, 5},
        {0x1e, 5}, {0x2, 2}, {0x1f, 5},
    },
    {
        {0x2, 2}, {0x6, 3}, {0x1c, 5}, {0x1d, 5},
        {0x1e, 5}, {0x0, 1}, {0x1f, 5},
    },
};

static const uint32_t SQ21_HRN0_SHORT_CODE[26] = {
    0x00, 0x00, 0x02, 0x03, 0x08, 0x09, 0x14, 0x15,
    0x16, 0x17, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65,
    0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d,
    0x6e, 0x6f,
};

static const uint8_t SQ21_HRN0_SHORT_BITS[26] = {
    0, 2, 3, 3, 4, 4, 5, 5, 5, 5, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};

static const int SQ21_HRN0_GROUP[] = {
    0, 1, 2, 3, 4, 6, 10, 26, 34, 42, 50, 66, 98, 162, 674, 17058,
};

static const uint8_t SQ21_HRN0_ADDBITS[] = {
    0, 0, 0, 0, 1, 2, 4, 3, 3, 3, 4, 5, 6, 9, 14,
};

static uint8_t sq21_neares_func_lowres_enhanced(int dpi, int run)
{
    int limit;
    int value;

    if (dpi <= 0) {
        return (uint8_t)((-dpi <= run) ? 0x00 : 0xff);
    }

    if (dpi < 600) {
        limit = (dpi * 0x14 + 300) / 600;
    } else {
        limit = dpi / 0x96 + 0x10;
    }
    if (limit > 0xff) {
        limit = 0xff;
    }

    value = (run != 0) ? ((dpi >> 3) / run) : limit;
    if (value < 0) {
        value = 0;
    }
    if (value > limit) {
        value = limit;
    }
    return (uint8_t)value;
}

static int sq21_reserve(sq21_t *sq, size_t extra)
{
    size_t need;
    uint8_t *next;

    if (sq->out_size + extra <= sq->out_cap) {
        return 0;
    }
    need = sq->out_cap ? sq->out_cap : 256;
    while (need < sq->out_size + extra) {
        need *= 2;
    }
    next = realloc(sq->out, need);
    if (!next) {
        return -1;
    }
    sq->out = next;
    sq->out_cap = need;
    return 0;
}

static int sq21_put_byte(sq21_t *sq, uint8_t value)
{
    if (sq21_reserve(sq, 1) != 0) {
        return -1;
    }
    sq->out[sq->out_size++] = value;
    return 0;
}

static int sq21_emit_bits(sq21_t *sq, uint32_t code, int bits)
{
    int total;
    int rem;

    if (bits == 0) {
        return 0;
    }

    total = sq->bitbuf_len + bits;
    rem = total - 8;

    if (rem < 0) {
        sq->bitbuf |= code << (-rem);
        sq->bitbuf_len = rem & 7;
        return 0;
    }

    if (sq21_put_byte(sq, (uint8_t)((code >> rem) | sq->bitbuf)) != 0) {
        return -1;
    }

    rem = total - 16;
    while (rem >= 0) {
        if (sq21_put_byte(sq, (uint8_t)(code >> rem)) != 0) {
            return -1;
        }
        rem -= 8;
    }

    sq->bitbuf = code << ((-rem) & 31);
    sq->bitbuf_len = rem & 7;

    return 0;
}

static int sq21_emit_hid(sq21_t *sq, int order)
{
    return sq21_emit_bits(sq, SQ21_HID[sq->hid][order][0], (int)SQ21_HID[sq->hid][order][1]);
}

static int sq21_emit_hpe0(sq21_t *sq, uint8_t delta)
{
    return sq21_emit_bits(sq, SQ21_HPE0_CODE[delta], (int)SQ21_HPE0_BITS[delta]);
}

static int sq21_put_run(sq21_t *sq, int ord, int run)
{
    int group;

    if (sq21_emit_hid(sq, ord) != 0) {
        return -1;
    }

    if (run < 26) {
        return sq21_emit_bits(sq, SQ21_HRN0_SHORT_CODE[run], SQ21_HRN0_SHORT_BITS[run]);
    }

    group = 7;
    while (group + 1 < (int)(sizeof(SQ21_HRN0_GROUP) / sizeof(SQ21_HRN0_GROUP[0])) &&
           SQ21_HRN0_GROUP[group + 1] <= run) {
        group++;
    }

    if (sq21_emit_bits(sq, (uint32_t)(group + 0x31), 6) != 0) {
        return -1;
    }

    if (SQ21_HRN0_ADDBITS[group] != 0) {
        if (sq21_emit_bits(sq,
                           (uint32_t)(run - SQ21_HRN0_GROUP[group]),
                           SQ21_HRN0_ADDBITS[group]) != 0) {
            return -1;
        }
    }

    return 0;
}

static int sq21_update_hid(sq21_t *sq, int count)
{
    int next_hid;

    /* Vendor uses: ((int)(8 - count | count + 8) < 1)
     * which is true only when abs(count) > 8. HID updates are
     * suppressed for small per-line count drift. */
    if (count >= -8 && count <= 8) {
        return 0;
    }

    next_hid = (count < 0) ? 1 : 0;
    if (next_hid == sq->hid) {
        return 0;
    }

    sq->hid = next_hid;
    if (sq21_emit_hid(sq, 6) != 0) {
        return -1;
    }

    {
        int rem = sq->bitbuf_len - 7;
        if (rem >= 0) {
            if (sq21_put_byte(sq, (uint8_t)sq->bitbuf) != 0) {
                return -1;
            }
            rem = sq->bitbuf_len - 15;
            while (rem >= 0) {
                if (sq21_put_byte(sq, 0x00) != 0) {
                    return -1;
                }
                rem -= 8;
            }
            sq->bitbuf = 0;
        }
        /* Vendor unconditionally applies this transform after HID emit,
         * even when bitbuf_len < 7 (no flush). Verified at 0x0804ca7a. */
        sq->bitbuf_len = (sq->bitbuf_len - 7) & 7;
    }

    return 0;
}

static void sq21_fill_ntable(sq21_t *sq)
{
    int i;

    for (i = 0; i < SQ21_NTABLE_SIZE; i++) {
        sq->ntable[i] = sq21_neares_func_lowres_enhanced(sq->dpi, i);
    }
}

static void sq21_update_offset(sq21_t *sq)
{
    int current = sq->line & 1;
    int prev = current ^ 1;
    int delta;

    sq->pix[0] = sq->pixbuf + SQ21_MARGIN + current * sq->line_stride;
    sq->pix[1] = sq->pixbuf + SQ21_MARGIN + prev * sq->line_stride;
    sq->err[0] = sq->errbuf + SQ21_MARGIN + current * sq->line_stride;
    sq->err[1] = sq->errbuf + SQ21_MARGIN + prev * sq->line_stride;

    delta = (int)(sq->pix[1] - sq->pix[0]);
    sq->pred[0].offset = -1;
    sq->pred[1].offset = delta;
    sq->pred[2].offset = delta - 1;
    sq->pred[3].offset = delta + 1;
    sq->pred[4].offset = (sq->zoffset == 1) ? (delta + 2) : (-sq->zoffset);

    sq->weight[0].offset = -1;
    sq->weight[1].offset = delta;
    sq->weight[2].offset = delta + 1;
    sq->weight[3].offset = delta + 3;
}

static void sq21_copy_line(sq21_t *sq, const uint8_t *line)
{
    uint8_t *dst = (uint8_t *)sq->pix[0];
    int i;

    for (i = 0; i < sq->src_width; i++) {
        dst[0] = 0xff;
        dst[1] = line[0];
        dst[2] = line[1];
        dst[3] = line[2];
        dst += 4;
        line += 3;
    }

    for (; i < sq->width; i++) {
        dst[0] = 0xff;
        dst[1] = 0xff;
        dst[2] = 0xff;
        dst[3] = 0xff;
        dst += 4;
    }

    memset(sq->err[0], 0, (size_t)sq->width * sizeof(uint32_t));
}

static void sq21_hire_sentinel(sq21_t *sq, uint32_t *eol)
{
    uint8_t *sentinel = (uint8_t *)eol;
    uint8_t hire = 0;
    int i;

    for (i = 0; i < SQ21_PREDICTORS; i++) {
        uint8_t *ref = sentinel + sq->pred[i].offset * 4;
        if (*ref != 0xff) {
            hire |= (uint8_t)(1u << i);
        }
    }
    *sentinel = hire;
}

static void sq21_fire_sentinel(uint32_t *eol)
{
    *(uint8_t *)eol = 0xff;
}

static uint8_t sq21_abs_u8_diff(uint8_t a, uint8_t b)
{
    return (a >= b) ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static int8_t sq21_avg_s8(int8_t a, int8_t b)
{
    return (int8_t)(((int16_t)a + (int16_t)b) >> 1);
}

static int16_t sq21_dupbyte_shift(uint8_t value, int shift)
{
    int16_t word = (int16_t)(uint16_t)(value | ((uint16_t)value << 8));
    return (int16_t)(word >> shift);
}

static int8_t sq21_sat_i8(int value)
{
    if (value < -128) {
        return -128;
    }
    if (value > 127) {
        return 127;
    }
    return (int8_t)value;
}

static uint32_t sq21_predict_pixel_8_888(const sq21_t *sq, const int32_t *err, uint32_t pixel)
{
    uint32_t out = 0;
    int c;
    uint32_t centered = pixel ^ 0x80808080u;

    for (c = 0; c < 4; c++) {
        uint8_t cur = (uint8_t)(centered >> (c * 8));
        uint8_t e3 = (uint8_t)((uint32_t)err[sq->weight[3].offset] >> (c * 8));
        uint8_t e2 = (uint8_t)((uint32_t)err[sq->weight[2].offset] >> (c * 8));
        uint8_t e1 = (uint8_t)((uint32_t)err[sq->weight[1].offset] >> (c * 8));
        uint8_t e0 = (uint8_t)((uint32_t)err[sq->weight[0].offset] >> (c * 8));
        int sum = sq21_dupbyte_shift(cur, 5) +
                  sq21_dupbyte_shift(e3, 8) +
                  sq21_dupbyte_shift(e2, 8) +
                  sq21_dupbyte_shift(e1, 7) +
                  sq21_dupbyte_shift(e0, 6);
        int8_t packed = sq21_sat_i8(sum >> 3);
        uint8_t value = (uint8_t)packed ^ 0x80u;
        if (value > 0xfeu) {
            value = 0xffu;
        }
        out |= (uint32_t)value << (c * 8);
    }

    return out;
}

static uint32_t sq21_add_signed_bytes_clamped(uint32_t pixel, uint32_t delta)
{
    uint32_t out = 0;
    int c;

    for (c = 0; c < 4; c++) {
        int value = (int)(uint8_t)(pixel >> (c * 8)) + (int)(int8_t)(uint8_t)(delta >> (c * 8));
        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        out |= (uint32_t)(uint8_t)value << (c * 8);
    }

    return out;
}

static int32_t sq21_sub_bytes(uint32_t a, uint32_t b)
{
    uint32_t out = 0;
    int c;

    for (c = 0; c < 4; c++) {
        out |= (uint32_t)(uint8_t)(((uint8_t)(a >> (c * 8))) - ((uint8_t)(b >> (c * 8)))) << (c * 8);
    }
    return (int32_t)out;
}

static void sq21_update_offset_8(sq21_t *sq)
{
    int current = sq->line & 1;
    int prev = current ^ 1;
    int delta;

    sq->pix8[0] = sq->pix8buf + SQ21_MARGIN + current * sq->line_stride;
    sq->pix8[1] = sq->pix8buf + SQ21_MARGIN + prev * sq->line_stride;
    sq->err8[0] = sq->err8buf + SQ21_MARGIN + current * sq->line_stride;
    sq->err8[1] = sq->err8buf + SQ21_MARGIN + prev * sq->line_stride;

    delta = (int)(sq->pix8[1] - sq->pix8[0]);
    sq->pred[0].offset = -1;
    sq->pred[1].offset = delta;
    sq->pred[2].offset = delta - 1;
    sq->pred[3].offset = delta + 1;
    sq->pred[4].offset = (sq->zoffset == 1) ? (delta + 2) : (-sq->zoffset);
    sq->weight[0].offset = -1;
    sq->weight[1].offset = delta;
    sq->weight[2].offset = delta + 1;
    sq->weight[3].offset = delta + 3;
}

static void sq21_copy_line_8(sq21_t *sq, const uint8_t *line)
{
    uint8_t *dst = sq->pix8[0];

    memcpy(dst, line, (size_t)sq->src_width);
    if (sq->src_width < sq->width) {
        memset(dst + sq->src_width, 0xff, (size_t)(sq->width - sq->src_width));
    }
    memset(sq->err8[0], 0, (size_t)sq->width);
}

static void sq21_hire_sentinel_8(sq21_t *sq, uint8_t *eol)
{
    uint8_t hire = 0;
    int i;

    for (i = 0; i < SQ21_PREDICTORS; i++) {
        if (eol[sq->pred[i].offset] != 0xff) {
            hire |= (uint8_t)(1u << i);
        }
    }
    *eol = hire;
}

static void sq21_fire_sentinel_8(sq21_t *sq, uint8_t *eol)
{
    *eol = sq->outpix;
}

static uint8_t sq21_weighted_predict_8(const sq21_t *sq, const int8_t *err, uint8_t pixel)
{
    int sum = (int)err[sq->weight[3].offset] + (int)err[sq->weight[2].offset] +
              (((int)err[sq->weight[1].offset] + ((int)err[sq->weight[0].offset] * 2)) * 2);
    int value;

    if (sum < 0) {
        sum += 7;
    }
    value = (int)pixel + (sum >> 3);
    if (value < 0) {
        value = 0;
    } else if (value > 255) {
        value = 255;
    }
    return (uint8_t)value;
}

static int sq21_encode_line_8(sq21_t *sq, const uint8_t *line)
{
    uint8_t *pix;
    uint8_t *eol;
    int8_t *err;
    int count = 0;
    int skip_sm = 0;
    int run = sq->run;
    int code = sq->code;
    int shift = sq->shift;

    sq21_update_offset_8(sq);
    sq21_copy_line_8(sq, line);


    pix = sq->pix8[0];
    err = sq->err8[0];
    eol = pix + sq->width;
    sq21_hire_sentinel_8(sq, eol);

    while (pix < eol) {
        int neares = 0;
        int best_order = 0;
        int drun = 0;
        int run_match = 0;
        int i;

        if (run < SQ21_NTABLE_SIZE) {
            neares = sq->ntable[run];
        }

        if (neares != 0 && skip_sm != 0) {
            goto predictor_match_8;
        }

        goto run_scan_8;

predictor_match_8:
        if (neares != 0) {
            uint8_t original = *pix;
            uint8_t threshold = (uint8_t)(neares + 1);

            for (i = 0; i < SQ21_PREDICTORS; i++) {
                uint8_t diff = sq21_abs_u8_diff(original, pix[sq->order[i]->offset]);
                if (diff < threshold) {
                    best_order = i;
                    threshold = diff;
                }
            }

            if (threshold <= (uint8_t)neares) {
                int pred_off = sq->order[best_order]->offset;
                *pix = pix[pred_off];
                *err = (int8_t)(original - *pix);
                drun = (*pix == pix[pred_off]) ? 1 : 0;
                if (drun != 0) {
                    skip_sm = *err;
                    goto handle_run_8;
                }
            }

            *pix = sq21_weighted_predict_8(sq, err, *pix);
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
                run = 0;
            }
            if (sq21_emit_hid(sq, 5) != 0) {
                return -1;
            }
            if (sq21_emit_hpe0(sq, (uint8_t)(*pix - pix[-1])) != 0) {
                return -1;
            }
            count--;
            shift = 0;
            pix++;
            err++;
            continue;
        }

run_scan_8:
        if ((eol - pix) < 32) {
            sq21_hire_sentinel_8(sq, eol);
        }

        for (i = 0; i < SQ21_PREDICTORS; i++) {
            uint8_t *q = pix;
            if (sq->order[i]->id == 0) {
                uint8_t value = *pix;
                while (value == q[-1]) {
                    q++;
                    value = *q;
                }
            } else {
                uint8_t *ref = pix + sq->order[i]->offset;
                if (*pix == *ref) {
                    do {
                        q++;
                        ref++;
                    } while (*q == *ref);
                }
            }
            if ((int)(q - pix) > run_match) {
                run_match = (int)(q - pix);
                best_order = i;
            }
        }

        drun = run_match;
        if (run_match == 0) {
            if (neares != 0) {
                goto predictor_match_8;
            }
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
                run = 0;
            }
            if (sq21_emit_hid(sq, 5) != 0) {
                return -1;
            }
            if (sq21_emit_hpe0(sq, (uint8_t)(*pix - pix[-1])) != 0) {
                return -1;
            }
            skip_sm = 0;
            count--;
            shift = 0;
            pix++;
            err++;
            continue;
        }

handle_run_8:
        if ((run != 0) && (best_order == 0)) {
            run += drun;
        } else {
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
            }
            code = best_order - shift;
            if (code == 0) {
                count++;
            }
            run = drun;
            if (best_order != 0) {
                sq21_pred_t *tmp = sq->order[best_order];
                for (i = best_order; i > 0; i--) {
                    sq->order[i] = sq->order[i - 1];
                }
                sq->order[0] = tmp;
            }
        }

        while (run > 0x42a0) {
            if (sq21_put_run(sq, code, SQ21_RUN_CHUNK) != 0) {
                return -1;
            }
            count++;
            code = 0;
            shift = 0;
            run -= SQ21_RUN_CHUNK;
        }

        pix += drun;
        err += drun;
    }

    sq21_fire_sentinel_8(sq, eol);
    sq->run = run;
    sq->code = code;
    sq->shift = shift;
    if (sq21_update_hid(sq, count) != 0) {
        return -1;
    }
    sq->line++;
    return 0;
}

static uint8_t sq21_color_diff_8_888(uint32_t a, uint32_t b)
{
    uint8_t d1 = sq21_abs_u8_diff((uint8_t)(a >> 8), (uint8_t)(b >> 8));
    uint8_t d2 = sq21_abs_u8_diff((uint8_t)(a >> 16), (uint8_t)(b >> 16));
    uint8_t d3 = sq21_abs_u8_diff((uint8_t)(a >> 24), (uint8_t)(b >> 24));

    if (d2 > d1) d1 = d2;
    if (d3 > d1) d1 = d3;
    return d1;
}


static int sq21_encode_line_8_888(sq21_t *sq, const uint8_t *line)
{
    uint32_t *pix;
    uint32_t *eol;
    int32_t *err;
    int count = 0;
    int skip_sm = 0;
    int run = sq->run;
    int code = sq->code;
    int shift = sq->shift;

    sq21_update_offset(sq);
    sq21_copy_line(sq, line);



    pix = sq->pix[0];
    err = sq->err[0];
    eol = pix + sq->width;
    sq21_hire_sentinel(sq, eol);

    while (pix < eol) {
        int neares = 0;
        int best_order = 0;
        int drun = 0;
        int run_match = 0;
        int i;
        if (run < SQ21_NTABLE_SIZE) {
            neares = sq->ntable[run];
        }

        if (neares != 0 && skip_sm != 0) {
            goto predictor_match;
        }

        goto run_scan;

predictor_match:
        if (neares != 0) {
            uint8_t threshold = (uint8_t)(neares + 1);
            uint32_t original = *pix;

            for (i = 0; i < SQ21_PREDICTORS; i++) {
                uint32_t ref = pix[sq->order[i]->offset];
                uint8_t diff = sq21_color_diff_8_888(original, ref);
                if (diff < threshold) {
                    best_order = i;
                    threshold = diff;
                }
            }

            if (threshold <= (uint8_t)neares) {
                int pred_off = sq->order[best_order]->offset;
                uint32_t ref = pix[pred_off];
                *pix = (*pix & 0x000000ffu) | (ref & 0xffffff00u);
                *err = sq21_sub_bytes(original, *pix);
                drun = (*pix == pix[pred_off]) ? 1 : 0;
                if (drun != 0) {
                    skip_sm = *err;
                    goto handle_run;
                }
            }

            *pix = sq21_predict_pixel_8_888(sq, err, *pix);
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
                run = 0;
            }

            if (sq21_emit_hid(sq, 5) != 0) {
                return -1;
            }
            for (i = 0; i < 4; i++) {
                uint8_t cur = (uint8_t)(*pix >> (i * 8));
                uint8_t left = (uint8_t)(pix[-1] >> (i * 8));
                if (sq21_emit_hpe0(sq, (uint8_t)(cur - left)) != 0) {
                    return -1;
                }
            }

            count--;
            shift = 0;
            pix++;
            err++;
            continue;
        }

run_scan:
        if ((eol - pix) < 32) {
            sq21_hire_sentinel(sq, eol);
        }

        for (i = 0; i < SQ21_PREDICTORS; i++) {
            uint32_t *q = pix;
            if (sq->order[i]->id == 0) {
                uint32_t value = *pix;
                while (value == q[-1]) {
                    q++;
                    value = *q;
                }
            } else {
                uint32_t *ref = pix + sq->order[i]->offset;
                if (*pix == *ref) {
                    do {
                        q++;
                        ref++;
                    } while (*q == *ref);
                }
            }
            if ((int)(q - pix) > run_match) {
                run_match = (int)(q - pix);
                best_order = i;
            }
        }

        drun = run_match;
        if (run_match == 0) {
            if (neares != 0) {
                goto predictor_match;
            }

            {
            *pix = sq21_predict_pixel_8_888(sq, err, *pix);
            }
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
                run = 0;
            }

            if (sq21_emit_hid(sq, 5) != 0) {
                return -1;
            }
            for (i = 0; i < 4; i++) {
                uint8_t cur = (uint8_t)(*pix >> (i * 8));
                uint8_t left = (uint8_t)(pix[-1] >> (i * 8));
                if (sq21_emit_hpe0(sq, (uint8_t)(cur - left)) != 0) {
                    return -1;
                }
            }

            skip_sm = 0;
            count--;
            shift = 0;
            pix++;
            err++;
            continue;
        }

handle_run:
        if ((run != 0) && (best_order == 0)) {
            run += drun;
        } else {
            if (run != 0) {
                if (sq21_put_run(sq, code, run) != 0) {
                    return -1;
                }
                shift = (run % SQ21_RUN_CHUNK) != 0;
            }

            code = best_order - shift;
            if (code == 0) {
                count++;
            }
            run = drun;

            if (best_order != 0) {
                sq21_pred_t *tmp = sq->order[best_order];
                for (i = best_order; i > 0; i--) {
                    sq->order[i] = sq->order[i - 1];
                }
                sq->order[0] = tmp;
            }
        }

        while (run > 0x42a0) {
            if (sq21_put_run(sq, code, SQ21_RUN_CHUNK) != 0) {
                return -1;
            }
            count++;
            code = 0;
            shift = 0;
            run -= SQ21_RUN_CHUNK;
        }

        pix += drun;
        err += drun;
    }

    sq21_fire_sentinel(eol);
    sq->run = run;
    sq->code = code;
    sq->shift = shift;
    if (sq21_update_hid(sq, count) != 0) {
        return -1;
    }
    sq->line++;
    return 0;
}

static int sq21_finish(sq21_t *sq)
{
    uint32_t code;
    int bits;
    int rem;

    if (sq->run != 0) {
        if (sq21_put_run(sq, sq->code, sq->run) != 0) {
            return -1;
        }
    }
    code = SQ21_HID[sq->hid][6][0];
    bits = sq->bitbuf_len + (int)SQ21_HID[sq->hid][6][1];
    rem = bits - 8;
    if (rem < 0) {
        sq->bitbuf |= code << ((-rem) & 31);
        sq->bitbuf_len = rem & 7;
        bits = sq->bitbuf_len;
    } else {
        if (sq21_put_byte(sq, (uint8_t)((code >> rem) | sq->bitbuf)) != 0) {
            return -1;
        }
        for (rem = bits - 16; rem >= 0; rem -= 8) {
            if (sq21_put_byte(sq, (uint8_t)(code >> rem)) != 0) {
                return -1;
            }
        }
        sq->bitbuf = code << ((-rem) & 31);
        sq->bitbuf_len = rem & 7;
        bits = sq->bitbuf_len;
    }

    code = 7;
    rem = bits - 5;
    if (rem < 0) {
        sq->bitbuf |= code << ((-rem) & 31);
    } else {
        if (sq21_put_byte(sq, (uint8_t)((code >> rem) | sq->bitbuf)) != 0) {
            return -1;
        }
        for (rem = bits - 13; rem >= 0; rem -= 8) {
            if (sq21_put_byte(sq, (uint8_t)(code >> rem)) != 0) {
                return -1;
            }
        }
        sq->bitbuf = code << ((-rem) & 31);
    }
    sq->bitbuf_len = rem & 7;
    bits = sq->bitbuf_len;

    if (bits != 0) {
        code = (1u << (8 - bits)) - 1u;
        if (sq21_put_byte(sq, (uint8_t)(sq->bitbuf | code)) != 0) {
            return -1;
        }
        sq->bitbuf = code << 8;
        sq->bitbuf_len = 0;
    }
    return 0;
}

int sq21_compress_888(const uint8_t *rgb_data, int width, int height, int dpi,
                      uint8_t **out_data, size_t *out_size)
{
    sq21_t sq;
    size_t pixels;
    int i;

    memset(&sq, 0, sizeof(sq));
    sq.src_width = width;
    sq.width = (width + 7) & ~7;
    sq.height = height;
    sq.dpi = dpi;
    sq.zoffset = 1;
    sq.outpix = 0xff;
    sq.line_stride = sq.width + SQ21_EXTRA;

    pixels = (size_t)sq.line_stride * 2;
    sq.pixbuf = malloc(pixels * sizeof(*sq.pixbuf));
    sq.errbuf = malloc(pixels * sizeof(*sq.errbuf));
    if (!sq.pixbuf || !sq.errbuf) {
        free(sq.pixbuf);
        free(sq.errbuf);
        return -1;
    }

    for (i = 0; i < SQ21_PREDICTORS; i++) {
        sq.pred[i].id = i;
        sq.order[i] = &sq.pred[i];
    }
    for (i = 0; i < (int)pixels; i++) {
        sq.pixbuf[i] = 0xffffffffu;
        sq.errbuf[i] = 0;
    }
    sq21_fill_ntable(&sq);

    for (i = 0; i < height; i++) {
        if (sq21_encode_line_8_888(&sq, rgb_data + (size_t)i * (size_t)width * 3) != 0) {
            free(sq.out);
            free(sq.pixbuf);
            free(sq.errbuf);
            return -1;
        }
    }
    if (sq21_finish(&sq) != 0) {
        free(sq.out);
        free(sq.pixbuf);
        free(sq.errbuf);
        return -1;
    }

    free(sq.pixbuf);
    free(sq.errbuf);
    *out_data = sq.out;
    *out_size = sq.out_size;
    return 0;
}

int sq21_compress_8(const uint8_t *data, int line_size, int height, int dpi,
                    uint8_t **out_data, size_t *out_size)
{
    sq21_t sq;
    size_t pixels;
    int i;

    memset(&sq, 0, sizeof(sq));
    sq.src_width = line_size;
    sq.width = (line_size + 7) & ~7;
    sq.height = height;
    sq.dpi = dpi;
    sq.zoffset = 8;
    sq.outpix = 0x00;
    sq.line_stride = sq.width + SQ21_EXTRA;

    pixels = (size_t)sq.line_stride * 2;
    sq.pix8buf = malloc(pixels);
    sq.err8buf = malloc(pixels);
    if (!sq.pix8buf || !sq.err8buf) {
        free(sq.pix8buf);
        free(sq.err8buf);
        return -1;
    }

    for (i = 0; i < SQ21_PREDICTORS; i++) {
        sq.pred[i].id = i;
        sq.order[i] = &sq.pred[i];
    }
    memset(sq.pix8buf, 0x00, pixels);
    memset(sq.err8buf, 0x00, pixels);
    sq21_fill_ntable(&sq);

    for (i = 0; i < height; i++) {
        if (sq21_encode_line_8(&sq, data + (size_t)i * (size_t)line_size) != 0) {
            free(sq.out);
            free(sq.pix8buf);
            free(sq.err8buf);
            return -1;
        }
    }
    if (sq21_finish(&sq) != 0) {
        free(sq.out);
        free(sq.pix8buf);
        free(sq.err8buf);
        return -1;
    }

    free(sq.pix8buf);
    free(sq.err8buf);
    *out_data = sq.out;
    *out_size = sq.out_size;
    return 0;
}
