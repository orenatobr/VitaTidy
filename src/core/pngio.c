#include "pngio.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../../third_party/stb/stb_image.h"

int img_decode(const uint8_t *png, size_t len, Image *out)
{
    int w, h, comp;
    if (len > 0x7fffffff) return -1;
    if (!stbi_info_from_memory(png, (int)len, &w, &h, &comp)) return -1;
    if (w <= 0 || h <= 0 || w > IMG_MAX_SIDE || h > IMG_MAX_SIDE) return -1;
    uint8_t *px = stbi_load_from_memory(png, (int)len, &w, &h, &comp, 4);
    if (!px) return -1;
    out->w = w;
    out->h = h;
    out->rgba = px;
    return 0;
}

void img_free(Image *img)
{
    if (img->rgba) stbi_image_free(img->rgba);
    img->rgba = NULL;
}

int img_has_alpha(const Image *img)
{
    for (size_t i = 0; i < (size_t)img->w * img->h; i++)
        if (img->rgba[i * 4 + 3] != 255) return 1;
    return 0;
}

/* ---- PNG writer ---- */

typedef struct {
    uint8_t *d;
    size_t n, cap;
} Buf;

static int buf_put(Buf *b, const void *src, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1024;
        while (cap < b->n + n) cap *= 2;
        uint8_t *g = realloc(b->d, cap);
        if (!g) return -1;
        b->d = g;
        b->cap = cap;
    }
    if (n) memcpy(b->d + b->n, src, n);
    b->n += n;
    return 0;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int chunk(Buf *b, const char type[4], const uint8_t *data, size_t len)
{
    uint8_t hdr[8];
    be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    uint32_t crc = crc32(0, hdr + 4, 4);
    if (len) crc = crc32(crc, data, (uInt)len);
    uint8_t tail[4];
    be32(tail, crc);
    if (buf_put(b, hdr, 8) || (len && buf_put(b, data, len)) || buf_put(b, tail, 4)) return -1;
    return 0;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

static void apply_filter(int type, const uint8_t *cur, const uint8_t *prev, uint8_t *dst, size_t row, size_t bpp)
{
    for (size_t i = 0; i < row; i++) {
        int a = i >= bpp ? cur[i - bpp] : 0;
        int b = prev ? prev[i] : 0;
        int c = (prev && i >= bpp) ? prev[i - bpp] : 0;
        int pred = 0;
        switch (type) {
        case 1: pred = a; break;
        case 2: pred = b; break;
        case 3: pred = (a + b) / 2; break;
        case 4: pred = paeth(a, b, c); break;
        default: break;
        }
        dst[i] = (uint8_t)(cur[i] - pred);
    }
}

/* Builds the zlib-compressed scanline stream, choosing the best filter per row. */
static uint8_t *deflate_rows(const uint8_t *rows, size_t row, int h, size_t bpp, size_t *out_len)
{
    size_t raw_len = (row + 1) * (size_t)h;
    uint8_t *raw = malloc(raw_len);
    uint8_t *tmp = malloc(row);
    if (!raw || !tmp) { free(raw); free(tmp); return NULL; }

    for (int y = 0; y < h; y++) {
        const uint8_t *cur = rows + (size_t)y * row;
        const uint8_t *prev = y ? cur - row : NULL;
        uint8_t *dst = raw + (size_t)y * (row + 1);
        long best = -1;
        int best_type = 0;
        for (int t = 0; t < 5; t++) {
            apply_filter(t, cur, prev, tmp, row, bpp);
            long score = 0;
            for (size_t i = 0; i < row; i++) score += abs((int8_t)tmp[i]);
            if (best < 0 || score < best) { best = score; best_type = t; }
        }
        dst[0] = (uint8_t)best_type;
        apply_filter(best_type, cur, prev, dst + 1, row, bpp);
    }
    free(tmp);

    uLongf cap = compressBound((uLong)raw_len);
    uint8_t *z = malloc(cap);
    if (!z) { free(raw); return NULL; }
    int rc = compress2(z, &cap, raw, (uLong)raw_len, 9);
    free(raw);
    if (rc != Z_OK) { free(z); return NULL; }
    *out_len = cap;
    return z;
}

static uint8_t *write_png(int w, int h, int depth, int ctype, const uint8_t *rows, size_t row, size_t bpp,
                          const uint8_t *plte, int nplte, const uint8_t *trns, int ntrns, size_t *out_len)
{
    size_t zlen;
    uint8_t *z = deflate_rows(rows, row, h, bpp, &zlen);
    if (!z) return NULL;

    Buf b = {0};
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w);
    be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = (uint8_t)depth; ihdr[9] = (uint8_t)ctype; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;

    int bad = buf_put(&b, sig, 8) || chunk(&b, "IHDR", ihdr, 13);
    if (!bad && plte) bad = chunk(&b, "PLTE", plte, (size_t)nplte * 3);
    if (!bad && trns) bad = chunk(&b, "tRNS", trns, (size_t)ntrns);
    if (!bad) bad = chunk(&b, "IDAT", z, zlen) || chunk(&b, "IEND", NULL, 0);
    free(z);
    if (bad) { free(b.d); return NULL; }
    *out_len = b.n;
    return b.d;
}

uint8_t *png_encode_truecolor(const Image *img, size_t *out_len)
{
    int alpha = img_has_alpha(img);
    size_t bpp = alpha ? 4 : 3;
    size_t row = (size_t)img->w * bpp;
    uint8_t *rows = malloc(row * img->h);
    if (!rows) return NULL;
    for (size_t i = 0; i < (size_t)img->w * img->h; i++)
        memcpy(rows + i * bpp, img->rgba + i * 4, bpp);
    uint8_t *png = write_png(img->w, img->h, 8, alpha ? 6 : 2, rows, row, bpp, NULL, 0, NULL, 0, out_len);
    free(rows);
    return png;
}

/* ---- median cut palette ---- */

typedef struct {
    uint8_t c[4];
    uint32_t count;
} Col;

static int col_cmp(const void *a, const void *b)
{
    return memcmp(((const Col *)a)->c, ((const Col *)b)->c, 4);
}

static void sort_channel(Col *v, size_t n, int ch)
{
    for (size_t gap = n / 2; gap > 0; gap /= 2) {
        for (size_t i = gap; i < n; i++) {
            Col t = v[i];
            size_t j = i;
            while (j >= gap && v[j - gap].c[ch] > t.c[ch]) { v[j] = v[j - gap]; j -= gap; }
            v[j] = t;
        }
    }
}

typedef struct {
    size_t lo, hi; /* [lo, hi) into the unique color array */
} Box;

static int widest_channel(const Col *v, const Box *b, int *range)
{
    int best = 0, best_range = -1;
    for (int ch = 0; ch < 4; ch++) {
        int mn = 255, mx = 0;
        for (size_t i = b->lo; i < b->hi; i++) {
            if (v[i].c[ch] < mn) mn = v[i].c[ch];
            if (v[i].c[ch] > mx) mx = v[i].c[ch];
        }
        if (mx - mn > best_range) { best_range = mx - mn; best = ch; }
    }
    *range = best_range;
    return best;
}

uint8_t *png_encode_palette(const Image *img, int max_colors, size_t *out_len)
{
    size_t npix = (size_t)img->w * img->h;
    if (max_colors < 2 || max_colors > 256) return NULL;

    Col *all = malloc(npix * sizeof *all);
    if (!all) return NULL;
    for (size_t i = 0; i < npix; i++) {
        memcpy(all[i].c, img->rgba + i * 4, 4);
        if (all[i].c[3] == 0) memset(all[i].c, 0, 4); /* fully transparent pixels share one color */
        all[i].count = 1;
    }
    qsort(all, npix, sizeof *all, col_cmp);
    size_t nu = 0;
    for (size_t i = 0; i < npix; i++) {
        if (nu && memcmp(all[nu - 1].c, all[i].c, 4) == 0) all[nu - 1].count++;
        else all[nu++] = all[i];
    }

    Box boxes[256];
    int nbox = 1;
    boxes[0].lo = 0;
    boxes[0].hi = nu;
    while (nbox < max_colors) {
        int pick = -1, pick_range = 0, pick_ch = 0;
        for (int i = 0; i < nbox; i++) {
            if (boxes[i].hi - boxes[i].lo < 2) continue;
            int range, ch = widest_channel(all, &boxes[i], &range);
            if (pick < 0 || range > pick_range) { pick = i; pick_range = range; pick_ch = ch; }
        }
        if (pick < 0) break;
        Box *b = &boxes[pick];
        sort_channel(all + b->lo, b->hi - b->lo, pick_ch);
        uint64_t total = 0, run = 0;
        for (size_t i = b->lo; i < b->hi; i++) total += all[i].count;
        size_t mid = b->lo + 1;
        for (size_t i = b->lo; i < b->hi - 1; i++) {
            run += all[i].count;
            mid = i + 1;
            if (run * 2 >= total) break;
        }
        boxes[nbox].lo = mid;
        boxes[nbox].hi = b->hi;
        b->hi = mid;
        nbox++;
    }

    uint8_t pal[256][4];
    for (int i = 0; i < nbox; i++) {
        uint64_t sum[4] = {0, 0, 0, 0}, tot = 0;
        for (size_t k = boxes[i].lo; k < boxes[i].hi; k++) {
            for (int ch = 0; ch < 4; ch++) sum[ch] += (uint64_t)all[k].c[ch] * all[k].count;
            tot += all[k].count;
        }
        for (int ch = 0; ch < 4; ch++) pal[i][ch] = (uint8_t)((sum[ch] + tot / 2) / (tot ? tot : 1));
    }
    free(all);

    int depth = nbox <= 2 ? 1 : nbox <= 4 ? 2 : nbox <= 16 ? 4 : 8;
    size_t row = ((size_t)img->w * depth + 7) / 8;
    uint8_t *rows = calloc(row * img->h, 1);
    if (!rows) return NULL;
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < img->w; x++) {
            const uint8_t *p = img->rgba + ((size_t)y * img->w + x) * 4;
            int best = 0;
            long best_d = -1;
            for (int i = 0; i < nbox; i++) {
                long d = 0;
                for (int ch = 0; ch < 4; ch++) { long e = (long)p[ch] - pal[i][ch]; d += e * e; }
                if (best_d < 0 || d < best_d) { best_d = d; best = i; }
            }
            uint8_t *r = rows + (size_t)y * row;
            if (depth == 8) r[x] = (uint8_t)best;
            else r[(size_t)x * depth / 8] |= (uint8_t)(best << (8 - depth - ((size_t)x * depth % 8)));
        }
    }

    uint8_t plte[256 * 3], trns[256];
    int ntrns = 0;
    for (int i = 0; i < nbox; i++) {
        plte[i * 3] = pal[i][0]; plte[i * 3 + 1] = pal[i][1]; plte[i * 3 + 2] = pal[i][2];
        trns[i] = pal[i][3];
        if (pal[i][3] != 255) ntrns = i + 1;
    }
    uint8_t *png = write_png(img->w, img->h, depth, 3, rows, row, 1, plte, nbox, ntrns ? trns : NULL, ntrns, out_len);
    free(rows);
    return png;
}

uint8_t *png_encode_fit(const Image *img, size_t max_len, size_t *out_len)
{
    uint8_t *png = png_encode_truecolor(img, out_len);
    if (png && *out_len <= max_len) return png;
    free(png);
    static const int steps[] = {256, 64, 16};
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        png = png_encode_palette(img, steps[i], out_len);
        if (png && *out_len <= max_len) return png;
        free(png);
    }
    return NULL;
}
