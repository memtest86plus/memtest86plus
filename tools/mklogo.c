// SPDX-License-Identifier: GPL-2.0
//
// Converts a PNG into a gfx_image_t C source file (check system/gfx.h).
// Each pixel is mapped to the nearest palette colour plus a 4-bit alpha, then
// the pixel bytes are PackBits RLE compressed. Self-contained.
//
// Usage: mklogo input.png output.c symbol [--colours RRGGBB,RRGGBB,...] [--preview preview.png]
//               [--background TOPRGB,BOTTOMRGB]
//
// The splash logo is regenerated with:
//   cc -O2 mklogo.c -o mklogo
//   mklogo mt86plus_logo_white.png ../app/splash_logo.c splash_logo_white --preview preview_white.png

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------
// Constants
//------------------------------------------------------------------------------

#define MAX_COLOURS     3

#define MAX_PIXELS      (1 << 26)

#define MAX_CODE_BITS   15

#define DEFAULT_COLOURS     "ffffff,e9484a"
#define DEFAULT_BACKGROUND  "000000,000000"

static const uint8_t png_signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t code_length_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

//------------------------------------------------------------------------------
// Types
//------------------------------------------------------------------------------

typedef struct {
    uint8_t r, g, b;
} rgb_t;

typedef struct {
    const uint8_t   *in;
    size_t          in_len;
    size_t          in_pos;
    uint32_t        bit_buf;
    int             bit_cnt;
    uint8_t         *out;
    size_t          out_len;
    size_t          out_pos;
} inflate_t;

typedef struct {
    uint16_t        count[MAX_CODE_BITS + 1];
    uint16_t        symbol[288];
} huffman_t;

//------------------------------------------------------------------------------
// Private Variables
//------------------------------------------------------------------------------

static const char *prog_name = "mklogo";

static uint32_t crc_table[256];

//------------------------------------------------------------------------------
// Utilities
//------------------------------------------------------------------------------

static void die(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "%s: error: ", prog_name);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    exit(1);
}

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size ? size : 1);
    if (ptr == NULL) {
        die("out of memory");
    }
    return ptr;
}

static uint32_t get_be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

// Matches Python's // operator, which rounds towards minus infinity.
static int floor_div(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return q;
}

static void init_crc_table(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xedb88320 ^ (c >> 1) : c >> 1;
        }
        crc_table[n] = c;
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = crc_table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t len)
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    return b << 16 | a;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        die("cannot open '%s'", path);
    }
    size_t cap = 1 << 16, pos = 0;
    uint8_t *buf = xmalloc(cap);
    size_t n;
    while ((n = fread(buf + pos, 1, cap - pos, f)) > 0) {
        pos += n;
        if (pos == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
            if (buf == NULL) {
                die("out of memory");
            }
        }
    }
    if (ferror(f)) {
        die("cannot read '%s'", path);
    }
    fclose(f);
    *len = pos;
    return buf;
}

//------------------------------------------------------------------------------
// Inflate (RFC 1950/1951)
//------------------------------------------------------------------------------

static unsigned get_bits(inflate_t *s, int need)
{
    uint32_t val = s->bit_buf;
    while (s->bit_cnt < need) {
        if (s->in_pos >= s->in_len) {
            die("PNG image data is truncated");
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bit_cnt;
        s->bit_cnt += 8;
    }
    s->bit_buf = val >> need;
    s->bit_cnt -= need;
    return val & ((1u << need) - 1);
}

static void build_huffman(huffman_t *h, const uint8_t *lengths, int n)
{
    uint16_t offset[MAX_CODE_BITS + 1];

    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    int left = 1;
    for (int len = 1; len <= MAX_CODE_BITS; len++) {
        left = (left << 1) - h->count[len];
        if (left < 0) {
            die("PNG image data has an over-subscribed Huffman code");
        }
    }
    offset[1] = 0;
    for (int len = 1; len < MAX_CODE_BITS; len++) {
        offset[len + 1] = offset[len] + h->count[len];
    }
    for (int i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[offset[lengths[i]]++] = i;
        }
    }
}

static int decode_symbol(inflate_t *s, const huffman_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_CODE_BITS; len++) {
        code |= get_bits(s, 1);
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    die("PNG image data has an invalid Huffman code");
    return -1;
}

static void inflate_stored(inflate_t *s)
{
    s->bit_buf = 0;
    s->bit_cnt = 0;
    if (s->in_len - s->in_pos < 4) {
        die("PNG image data is truncated");
    }
    const uint8_t *p = s->in + s->in_pos;
    unsigned len = p[0] | p[1] << 8;
    if (len != (~(p[2] | p[3] << 8) & 0xffffu)) {
        die("PNG image data has a corrupt stored block");
    }
    s->in_pos += 4;
    if (s->in_len - s->in_pos < len) {
        die("PNG image data is truncated");
    }
    if (s->out_len - s->out_pos < len) {
        die("PNG image data is larger than expected");
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
}

static void inflate_codes(inflate_t *s, const huffman_t *lit, const huffman_t *dist)
{
    for (;;) {
        int sym = decode_symbol(s, lit);
        if (sym < 256) {
            if (s->out_pos >= s->out_len) {
                die("PNG image data is larger than expected");
            }
            s->out[s->out_pos++] = sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) {
                die("PNG image data has an invalid length code");
            }
            size_t len = length_base[sym] + get_bits(s, length_extra[sym]);
            sym = decode_symbol(s, dist);
            if (sym >= 30) {
                die("PNG image data has an invalid distance code");
            }
            size_t d = dist_base[sym] + get_bits(s, dist_extra[sym]);
            if (d > s->out_pos) {
                die("PNG image data has a distance too far back");
            }
            if (s->out_len - s->out_pos < len) {
                die("PNG image data is larger than expected");
            }
            for (size_t i = 0; i < len; i++, s->out_pos++) {
                s->out[s->out_pos] = s->out[s->out_pos - d];
            }
        }
    }
}

static void inflate_fixed(inflate_t *s)
{
    static huffman_t lit, dist;
    static bool built = false;

    if (!built) {
        uint8_t lengths[288];
        int i = 0;
        for (; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        build_huffman(&lit, lengths, 288);
        memset(lengths, 5, 30);
        build_huffman(&dist, lengths, 30);
        built = true;
    }
    inflate_codes(s, &lit, &dist);
}

static void inflate_dynamic(inflate_t *s)
{
    huffman_t lit, dist;
    uint8_t lengths[288 + 32];

    int num_lit  = get_bits(s, 5) + 257;
    int num_dist = get_bits(s, 5) + 1;
    int num_code = get_bits(s, 4) + 4;
    if (num_lit > 286 || num_dist > 30) {
        die("PNG image data has a corrupt dynamic block header");
    }

    memset(lengths, 0, 19);
    for (int i = 0; i < num_code; i++) {
        lengths[code_length_order[i]] = get_bits(s, 3);
    }
    build_huffman(&lit, lengths, 19);

    int i = 0;
    while (i < num_lit + num_dist) {
        int sym = decode_symbol(s, &lit);
        int len = 0, repeat;
        if (sym < 16) {
            lengths[i++] = sym;
            continue;
        } else if (sym == 16) {
            if (i == 0) {
                die("PNG image data repeats a missing code length");
            }
            len = lengths[i - 1];
            repeat = 3 + get_bits(s, 2);
        } else if (sym == 17) {
            repeat = 3 + get_bits(s, 3);
        } else {
            repeat = 11 + get_bits(s, 7);
        }
        if (i + repeat > num_lit + num_dist) {
            die("PNG image data has too many code lengths");
        }
        while (repeat--) {
            lengths[i++] = len;
        }
    }
    if (lengths[256] == 0) {
        die("PNG image data has no end-of-block code");
    }
    build_huffman(&lit, lengths, num_lit);
    build_huffman(&dist, lengths + num_lit, num_dist);
    inflate_codes(s, &lit, &dist);
}

static void zlib_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    if (in_len < 6 || (in[0] & 0x0f) != 8 || (in[0] >> 4) > 7 || ((in[0] << 8) | in[1]) % 31 != 0) {
        die("PNG image data has an invalid zlib header");
    }
    if (in[1] & 0x20) {
        die("PNG image data uses a preset dictionary");
    }

    inflate_t s = { .in = in, .in_len = in_len, .in_pos = 2, .out = out, .out_len = out_len };
    bool last;
    do {
        last = get_bits(&s, 1);
        switch (get_bits(&s, 2)) {
          case 0:  inflate_stored(&s);  break;
          case 1:  inflate_fixed(&s);   break;
          case 2:  inflate_dynamic(&s); break;
          default: die("PNG image data has an invalid block type");
        }
    } while (!last);

    if (s.out_pos != out_len) {
        die("PNG image data is smaller than expected");
    }
    if (s.in_len - s.in_pos < 4 || get_be32(s.in + s.in_pos) != adler32(out, out_len)) {
        die("PNG image data has a bad Adler-32 checksum");
    }
}

//------------------------------------------------------------------------------
// PNG reader/writer
//------------------------------------------------------------------------------

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static unsigned get_sample(const uint8_t *row, int index, int depth)
{
    if (depth == 16) {
        return row[index * 2] << 8 | row[index * 2 + 1];
    }
    if (depth == 8) {
        return row[index];
    }
    int bit = index * depth;
    return (row[bit / 8] >> (8 - depth - bit % 8)) & ((1 << depth) - 1);
}

// Returns the image as 8-bit RGBA, like PIL's convert('RGBA').
static uint8_t *read_png(const char *path, int *width, int *height)
{
    size_t file_len;
    uint8_t *file = read_file(path, &file_len);

    if (file_len < 8 || memcmp(file, png_signature, 8) != 0) {
        die("'%s' is not a PNG file", path);
    }

    uint32_t w = 0, h = 0;
    int depth = 0, colour_type = -1;
    rgb_t palette[256];
    uint8_t palette_alpha[256];
    int palette_size = 0;
    bool has_trns = false;
    unsigned trns[3] = { 0 };
    uint8_t *idat = NULL;
    size_t idat_len = 0;
    bool seen_iend = false;

    memset(palette_alpha, 0xff, sizeof(palette_alpha));

    size_t pos = 8;
    while (!seen_iend) {
        if (file_len - pos < 12) {
            die("'%s' is truncated", path);
        }
        uint32_t len = get_be32(file + pos);
        const uint8_t *type = file + pos + 4;
        const uint8_t *data = file + pos + 8;
        if (len > file_len - pos - 12) {
            die("'%s' is truncated", path);
        }
        if (crc32_update(0, type, len + 4) != get_be32(data + len)) {
            die("'%s' has a bad CRC in chunk '%.4s'", path, (const char *)type);
        }
        pos += len + 12;

        if (memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) {
                die("'%s' has a corrupt IHDR chunk", path);
            }
            w = get_be32(data);
            h = get_be32(data + 4);
            depth = data[8];
            colour_type = data[9];
            if (data[10] != 0 || data[11] != 0) {
                die("'%s' uses an unknown compression or filter method", path);
            }
            if (data[12] != 0) {
                die("'%s' is interlaced, which is not supported", path);
            }
            bool valid;
            switch (colour_type) {
              case 0:  valid = depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16; break;
              case 3:  valid = depth == 1 || depth == 2 || depth == 4 || depth == 8; break;
              case 2:
              case 4:
              case 6:  valid = depth == 8 || depth == 16; break;
              default: valid = false;
            }
            if (!valid) {
                die("'%s' has an invalid colour type/bit depth (%d/%d)", path, colour_type, depth);
            }
            if (w == 0 || h == 0 || w > MAX_PIXELS || h > MAX_PIXELS || (uint64_t)w * h > MAX_PIXELS) {
                die("'%s' has unsupported dimensions %ux%u", path, w, h);
            }
        } else if (colour_type < 0) {
            die("'%s' does not start with an IHDR chunk", path);
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (len % 3 != 0 || len == 0 || len > 768) {
                die("'%s' has a corrupt PLTE chunk", path);
            }
            palette_size = len / 3;
            for (int i = 0; i < palette_size; i++) {
                palette[i] = (rgb_t){ data[i * 3], data[i * 3 + 1], data[i * 3 + 2] };
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            has_trns = true;
            if (colour_type == 3 && len <= 256) {
                memcpy(palette_alpha, data, len);
            } else if (colour_type == 0 && len == 2) {
                trns[0] = data[0] << 8 | data[1];
            } else if (colour_type == 2 && len == 6) {
                for (int i = 0; i < 3; i++) {
                    trns[i] = data[i * 2] << 8 | data[i * 2 + 1];
                }
            } else {
                die("'%s' has a corrupt tRNS chunk", path);
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            idat = realloc(idat, idat_len + len + 1);
            if (idat == NULL) {
                die("out of memory");
            }
            memcpy(idat + idat_len, data, len);
            idat_len += len;
        } else if (memcmp(type, "IEND", 4) == 0) {
            seen_iend = true;
        } else if (!(type[0] & 0x20)) {
            die("'%s' has an unknown critical chunk '%.4s'", path, (const char *)type);
        }
    }
    if (idat == NULL) {
        die("'%s' has no image data", path);
    }
    if (colour_type == 3 && palette_size == 0) {
        die("'%s' has no palette", path);
    }

    static const int channel_count[7] = { 1, 0, 3, 1, 2, 0, 4 };
    int channels = channel_count[colour_type];
    int pixel_bits = channels * depth;
    int filter_bpp = pixel_bits < 8 ? 1 : pixel_bits / 8;
    size_t stride = ((size_t)w * pixel_bits + 7) / 8;

    uint8_t *raw = xmalloc((stride + 1) * h);
    zlib_decompress(idat, idat_len, raw, (stride + 1) * h);
    free(idat);

    uint8_t *rgba = xmalloc((size_t)w * h * 4);
    uint8_t *prev = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + y * (stride + 1) + 1;
        int filter = row[-1];
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)filter_bpp ? row[i - filter_bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= (size_t)filter_bpp) ? prev[i - filter_bpp] : 0;
            switch (filter) {
              case 0:  break;
              case 1:  row[i] += a; break;
              case 2:  row[i] += b; break;
              case 3:  row[i] += (a + b) / 2; break;
              case 4:  row[i] += paeth(a, b, c); break;
              default: die("'%s' has an invalid filter type %d on row %u", path, filter, y);
            }
        }
        prev = row;

        int shift = depth == 16 ? 8 : 0;
        int scale = depth < 8 ? 255 / ((1 << depth) - 1) : 1;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t *px = rgba + ((size_t)y * w + x) * 4;
            unsigned s[4];
            for (int ch = 0; ch < channels; ch++) {
                s[ch] = get_sample(row, x * channels + ch, depth);
            }
            switch (colour_type) {
              case 0:
                px[0] = px[1] = px[2] = (s[0] >> shift) * scale;
                px[3] = (has_trns && s[0] == trns[0]) ? 0 : 255;
                break;
              case 2:
                for (int ch = 0; ch < 3; ch++) {
                    px[ch] = s[ch] >> shift;
                }
                px[3] = (has_trns && s[0] == trns[0] && s[1] == trns[1] && s[2] == trns[2]) ? 0 : 255;
                break;
              case 3:
                if ((int)s[0] >= palette_size) {
                    die("'%s' has an out of range palette index %u", path, s[0]);
                }
                px[0] = palette[s[0]].r;
                px[1] = palette[s[0]].g;
                px[2] = palette[s[0]].b;
                px[3] = palette_alpha[s[0]];
                break;
              case 4:
                px[0] = px[1] = px[2] = s[0] >> shift;
                px[3] = s[1] >> shift;
                break;
              case 6:
                for (int ch = 0; ch < 4; ch++) {
                    px[ch] = s[ch] >> shift;
                }
                break;
            }
        }
    }
    free(raw);
    free(file);

    *width = w;
    *height = h;
    return rgba;
}

static void write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t header[8];
    put_be32(header, len);
    memcpy(header + 4, type, 4);
    uint32_t crc = crc32_update(crc32_update(0, header + 4, 4), data, len);
    uint8_t trailer[4];
    put_be32(trailer, crc);
    fwrite(header, 1, 8, f);
    if (len != 0) {
        fwrite(data, 1, len, f);
    }
    fwrite(trailer, 1, 4, f);
}

// Writes an 8-bit RGB PNG using uncompressed deflate blocks.
static void write_png(const char *path, const uint8_t *rgb, int width, int height)
{
    size_t raw_len = ((size_t)width * 3 + 1) * height;
    uint8_t *raw = xmalloc(raw_len);
    for (int y = 0; y < height; y++) {
        uint8_t *row = raw + y * ((size_t)width * 3 + 1);
        row[0] = 0;
        memcpy(row + 1, rgb + (size_t)y * width * 3, (size_t)width * 3);
    }

    size_t blocks = raw_len / 65535 + 1;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    uint8_t *z = xmalloc(z_len);
    size_t zp = 0;
    z[zp++] = 0x78;
    z[zp++] = 0x01;
    for (size_t pos = 0; pos < raw_len; ) {
        size_t n = raw_len - pos > 65535 ? 65535 : raw_len - pos;
        z[zp++] = pos + n == raw_len;
        z[zp++] = n & 0xff;
        z[zp++] = n >> 8;
        z[zp++] = ~n & 0xff;
        z[zp++] = (~n >> 8) & 0xff;
        memcpy(z + zp, raw + pos, n);
        zp += n;
        pos += n;
    }
    put_be32(z + zp, adler32(raw, raw_len));
    zp += 4;

    uint8_t ihdr[13];
    put_be32(ihdr, width);
    put_be32(ihdr + 4, height);
    ihdr[8]  = 8;   // bit depth
    ihdr[9]  = 2;   // RGB
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        die("cannot create '%s'", path);
    }
    fwrite(png_signature, 1, 8, f);
    write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    write_chunk(f, "IDAT", z, zp);
    write_chunk(f, "IEND", NULL, 0);
    if (ferror(f) || fclose(f) != 0) {
        die("cannot write '%s'", path);
    }
    free(z);
    free(raw);
}

//------------------------------------------------------------------------------
// Logo encoding
//------------------------------------------------------------------------------

static void classify(const uint8_t *rgba, size_t count, const rgb_t *colours, int num_colours, uint8_t *values)
{
    for (size_t i = 0; i < count; i++) {
        const uint8_t *px = rgba + i * 4;
        int alpha = (px[3] * 15 + 127) / 255;
        if (alpha == 0) {
            values[i] = 0;
            continue;
        }
        int best = 0, best_dist = 0;
        for (int c = 0; c < num_colours; c++) {
            int dr = px[0] - colours[c].r, dg = px[1] - colours[c].g, db = px[2] - colours[c].b;
            int dist = dr * dr + dg * dg + db * db;
            if (c == 0 || dist < best_dist) {
                best = c;
                best_dist = dist;
            }
        }
        values[i] = (best + 1) << 4 | alpha;
    }
}

static size_t packbits(const uint8_t *values, size_t n, uint8_t *out)
{
    size_t len = 0, i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && j - i < 128 && values[j] == values[i]) {
            j++;
        }
        if (j - i >= 2) {
            out[len++] = 0x80 | (j - i - 1);
            out[len++] = values[i];
            i = j;
            continue;
        }
        j = i;
        while (j < n && j - i < 128 && !(j + 1 < n && values[j] == values[j + 1])) {
            j++;
        }
        if (j == i) {
            j = i + 1;
        }
        out[len++] = j - i - 1;
        memcpy(out + len, values + i, j - i);
        len += j - i;
        i = j;
    }
    return len;
}

static bool unpackbits_matches(const uint8_t *data, size_t len, const uint8_t *values, size_t count)
{
    size_t i = 0, n = 0;
    while (n < count) {
        if (i >= len) {
            return false;
        }
        int header = data[i++];
        size_t run = (header & 0x7f) + 1;
        if (n + run > count) {
            return false;
        }
        if (header & 0x80) {
            if (i >= len) {
                return false;
            }
            for (size_t k = 0; k < run; k++) {
                if (values[n++] != data[i]) {
                    return false;
                }
            }
            i++;
        } else {
            if (len - i < run || memcmp(values + n, data + i, run) != 0) {
                return false;
            }
            i += run;
            n += run;
        }
    }
    return i == len;
}

static void write_preview(const char *path, const uint8_t *values, int width, int height,
                          const rgb_t *colours, const rgb_t *background)
{
    uint8_t *rgb = xmalloc((size_t)width * height * 3);
    int span = height > 1 ? height - 1 : 1;
    for (int y = 0; y < height; y++) {
        int bg[3] = {
            background[0].r + floor_div((background[1].r - background[0].r) * y, span),
            background[0].g + floor_div((background[1].g - background[0].g) * y, span),
            background[0].b + floor_div((background[1].b - background[0].b) * y, span)
        };
        for (int x = 0; x < width; x++) {
            uint8_t v = values[(size_t)y * width + x];
            uint8_t *px = rgb + ((size_t)y * width + x) * 3;
            if (v >> 4) {
                const rgb_t *fg = &colours[(v >> 4) - 1];
                int a = v & 0xf;
                px[0] = bg[0] + floor_div((fg->r - bg[0]) * a, 15);
                px[1] = bg[1] + floor_div((fg->g - bg[1]) * a, 15);
                px[2] = bg[2] + floor_div((fg->b - bg[2]) * a, 15);
            } else {
                px[0] = bg[0];
                px[1] = bg[1];
                px[2] = bg[2];
            }
        }
    }
    write_png(path, rgb, width, height);
    free(rgb);
}

static void write_source(const char *path, const char *symbol, int width, int height,
                         const uint8_t *data, size_t len, const rgb_t *colours, int num_colours)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        die("cannot create '%s'", path);
    }
    fprintf(f, "// SPDX-License-Identifier: GPL-2.0\n");
    fprintf(f, "// Copyright (C) 2026 Sam Demeulemeester.\n");
    fprintf(f, "//\n");
    fprintf(f, "// Generated by tools/mklogo.c - do not edit.\n\n");
    fprintf(f, "#include <stdint.h>\n\n");
    fprintf(f, "#include \"gfx.h\"\n\n");
    fprintf(f, "static const uint8_t %s_data[%zu] = {\n", symbol, len);
    for (size_t i = 0; i < len; i += 16) {
        fprintf(f, "   ");
        for (size_t j = i; j < len && j < i + 16; j++) {
            fprintf(f, " 0x%02x,", data[j]);
        }
        fprintf(f, "\n");
    }
    fprintf(f, "};\n\n");
    fprintf(f, "const gfx_image_t %s = {\n", symbol);
    fprintf(f, "    .width   = %d,\n", width);
    fprintf(f, "    .height  = %d,\n", height);
    fprintf(f, "    .colours = { 0x000000");
    for (int i = 0; i < MAX_COLOURS; i++) {
        if (i < num_colours) {
            fprintf(f, ", 0x%02x%02x%02x", colours[i].r, colours[i].g, colours[i].b);
        } else {
            fprintf(f, ", 0x000000");
        }
    }
    fprintf(f, " },\n");
    fprintf(f, "    .data    = %s_data\n", symbol);
    fprintf(f, "};\n");
    if (ferror(f) || fclose(f) != 0) {
        remove(path);
        die("cannot write '%s'", path);
    }
}

//------------------------------------------------------------------------------
// Command line
//------------------------------------------------------------------------------

static void usage(FILE *f)
{
    fprintf(f, "usage: %s input.png output.c symbol [--colours RRGGBB,RRGGBB,...] [--preview preview.png]\n"
               "       %*s [--background TOPRGB,BOTTOMRGB]\n", prog_name, (int)strlen(prog_name), "");
}

static int parse_colour_list(const char *option, const char *list, rgb_t *colours, int max_colours)
{
    int count = 0;
    const char *p = list;
    for (;;) {
        unsigned value = 0;
        int digits = 0;
        for (; digits < 7 && p[digits] != ',' && p[digits] != '\0'; digits++) {
            char c = p[digits];
            int nibble = c >= '0' && c <= '9' ? c - '0'
                       : c >= 'a' && c <= 'f' ? c - 'a' + 10
                       : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (nibble < 0) {
                digits = 7;
                break;
            }
            value = value << 4 | nibble;
        }
        if (digits != 6) {
            die("%s: invalid colour list '%s' (expected RRGGBB,RRGGBB,...)", option, list);
        }
        if (count == max_colours) {
            return max_colours + 1;
        }
        colours[count++] = (rgb_t){ value >> 16, value >> 8, value };
        p += 6;
        if (*p == '\0') {
            return count;
        }
        p++;
    }
}

static bool is_identifier(const char *s)
{
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_')) {
        return false;
    }
    for (s++; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) {
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    const char *positional[3];
    int num_positional = 0;
    const char *colours_arg = DEFAULT_COLOURS;
    const char *background_arg = DEFAULT_BACKGROUND;
    const char *preview_path = NULL;

    if (argc > 0 && argv[0][0] != '\0') {
        prog_name = strrchr(argv[0], '/') ? strrchr(argv[0], '/') + 1 : argv[0];
    }

    for (int i = 1; i < argc; i++) {
        static const char *const options[3] = { "--colours", "--background", "--preview" };
        const char **targets[3] = { &colours_arg, &background_arg, &preview_path };
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            if (num_positional == 3) {
                usage(stderr);
                die("unexpected argument '%s'", arg);
            }
            positional[num_positional++] = arg;
            continue;
        }
        int opt = -1;
        const char *value = NULL;
        for (int k = 0; k < 3; k++) {
            size_t len = strlen(options[k]);
            if (strncmp(arg, options[k], len) == 0 && (arg[len] == '\0' || arg[len] == '=')) {
                opt = k;
                value = arg[len] == '=' ? arg + len + 1 : NULL;
            }
        }
        if (opt < 0) {
            usage(stderr);
            die("unknown option '%s'", arg);
        }
        if (value == NULL) {
            if (++i == argc) {
                usage(stderr);
                die("%s requires a value", options[opt]);
            }
            value = argv[i];
        }
        *targets[opt] = value;
    }
    if (num_positional != 3) {
        usage(stderr);
        die("expected input, output and symbol arguments");
    }
    const char *input_path  = positional[0];
    const char *output_path = positional[1];
    const char *symbol      = positional[2];

    if (!is_identifier(symbol)) {
        die("'%s' is not a valid C identifier", symbol);
    }

    rgb_t colours[MAX_COLOURS];
    int num_colours = parse_colour_list("--colours", colours_arg, colours, MAX_COLOURS);
    if (num_colours > MAX_COLOURS) {
        die("at most %d colours are supported", MAX_COLOURS);
    }
    rgb_t background[2];
    if (parse_colour_list("--background", background_arg, background, 2) != 2) {
        die("--background requires exactly 2 colours (TOPRGB,BOTTOMRGB)");
    }

    init_crc_table();

    int width, height;
    uint8_t *rgba = read_png(input_path, &width, &height);
    size_t count = (size_t)width * height;

    uint8_t *values = xmalloc(count);
    classify(rgba, count, colours, num_colours, values);
    free(rgba);

    uint8_t *data = xmalloc(count * 2 + 2);
    size_t len = packbits(values, count, data);
    if (!unpackbits_matches(data, len, values, count)) {
        die("internal error: PackBits round trip failed");
    }

    if (preview_path != NULL) {
        write_preview(preview_path, values, width, height, colours, background);
    }

    write_source(output_path, symbol, width, height, data, len, colours, num_colours);

    printf("%s: %dx%d, %zu bytes\n", output_path, width, height, len);

    free(data);
    free(values);
    return 0;
}
