/*
 * Developer tool, read-only on its input.
 *   icon_cli iso game.iso out.png   extracts ICON0, badges it exactly as the app would (size-fitted) and writes out.png
 *   icon_cli png in.png out.png     badges a PNG file
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/badge.h"
#include "../src/core/iso9660.h"
#include "../src/core/pngio.h"
#include "../src/core/vt.h"

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s iso|png in out.png\n", argv[0]); return 2; }
    uint8_t *png;
    size_t len;
    uint32_t region = 0;
    char err[128] = "";
    if (strcmp(argv[1], "iso") == 0) {
        FILE *f = fopen(argv[2], "rb");
        uint64_t off;
        if (!f || iso_find_icon0(f, &off, &region, err, sizeof err) != 0) { fprintf(stderr, "iso: %s\n", err); return 1; }
        len = region;
        png = malloc(len);
        if (iso_read_at(f, off, png, len) != 0) return 1;
        fclose(f);
        printf("offset=%llu length=%u\n", (unsigned long long)off, region);
    } else if (fs_read_all(argv[2], &png, &len, 1 << 24) != 0) {
        return 1;
    }
    Image img;
    if (img_decode(png, len, &img) != 0) { fprintf(stderr, "decode failed\n"); return 1; }
    printf("icon %dx%d alpha=%d\n", img.w, img.h, img_has_alpha(&img));
    Badge b = badge_for(img.w, img.h);
    badge_draw(&img, &b);
    size_t out_len;
    uint8_t *out = region ? png_encode_fit(&img, region, &out_len) : png_encode_truecolor(&img, &out_len);
    if (!out) { fprintf(stderr, "does not fit in %u bytes\n", region); return 1; }
    printf("badged png %zu bytes (budget %u)\n", out_len, region);
    return fs_write_all(argv[3], out, out_len) == 0 ? 0 : 1;
}
