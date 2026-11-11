#include "iso9660.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "vt.h"

/* Directory Record offsets (ECMA-119, section 9.1). */
enum { DR_LBA = 2, DR_LEN = 10, DR_FLAGS = 25, DR_NAME_LEN = 32, DR_NAME = 33 };
enum { PVD_ROOT = 156, ROOT_LEN = 34 };
/* A directory extent beyond this is treated as a corrupt image. */
#define MAX_DIR_BYTES (4u * 1024 * 1024)

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Offsets must fit off_t, which is 32 bit on some targets (PSP discs stay under 2 GiB). */
static int offset_ok(uint64_t offset, size_t length)
{
    uint64_t limit = sizeof(off_t) >= 8 ? (uint64_t)INT64_MAX : (uint64_t)INT32_MAX;
    return offset <= limit && length <= limit - offset;
}

int iso_read_at(FILE *f, uint64_t offset, void *buf, size_t length)
{
    if (!offset_ok(offset, length)) return -1;
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) return -1;
    return fread(buf, 1, length, f) == length ? 0 : -1;
}

int iso_write_at(FILE *f, uint64_t offset, const void *buf, size_t length)
{
    if (!offset_ok(offset, length)) return -1;
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) return -1;
    if (fwrite(buf, 1, length, f) != length) return -1;
    return fflush(f) == 0 ? 0 : -1;
}

static uint8_t *read_extent(FILE *f, uint32_t lba, uint32_t length)
{
    if (length == 0 || length > MAX_DIR_BYTES) return NULL;
    uint8_t *data = malloc(length);
    if (!data) return NULL;
    if (iso_read_at(f, (uint64_t)lba * ISO_SECTOR, data, length) != 0) { free(data); return NULL; }
    return data;
}

/* Finds name in a directory extent; the ";1" version suffix is ignored. */
static int find_entry(const uint8_t *data, uint32_t size, const char *target, int want_dir, uint32_t *lba, uint32_t *len)
{
    uint32_t pos = 0;
    size_t tlen = strlen(target);
    while (pos < size) {
        uint8_t rec = data[pos];
        if (rec == 0) {
            pos = (pos / ISO_SECTOR + 1) * ISO_SECTOR; /* the rest of the sector is padding */
            continue;
        }
        if (rec < DR_NAME || pos + rec > size) return -1;
        uint8_t nlen = data[pos + DR_NAME_LEN];
        if ((uint32_t)DR_NAME + nlen > rec) return -1;
        const char *name = (const char *)data + pos + DR_NAME;
        int is_dir = (data[pos + DR_FLAGS] & 0x02) != 0;
        size_t cmp = 0;
        while (cmp < nlen && name[cmp] != ';') cmp++;
        if (!(nlen == 1 && (name[0] == 0 || name[0] == 1)) && is_dir == want_dir && cmp == tlen &&
            strncasecmp(name, target, tlen) == 0) {
            *lba = le32(data + pos + DR_LBA);
            *len = le32(data + pos + DR_LEN);
            return 0;
        }
        pos += rec;
    }
    return -1;
}

int iso_find_icon0(FILE *f, uint64_t *offset, uint32_t *length, char *err, size_t errlen)
{
    uint8_t pvd[ISO_SECTOR];
    int found = 0;
    for (uint32_t lba = 16; lba < 16 + 32; lba++) {
        if (iso_read_at(f, (uint64_t)lba * ISO_SECTOR, pvd, sizeof pvd) != 0 || memcmp(pvd + 1, "CD001", 5) != 0) {
            VT_ERR(err, errlen, "not an ISO9660 image");
            return -1;
        }
        if (pvd[0] == 1) { found = 1; break; }
        if (pvd[0] == 255) break;
    }
    if (!found) { VT_ERR(err, errlen, "no primary volume descriptor"); return -1; }

    uint32_t root_lba = le32(pvd + PVD_ROOT + DR_LBA), root_len = le32(pvd + PVD_ROOT + DR_LEN);
    (void)ROOT_LEN;
    uint8_t *root = read_extent(f, root_lba, root_len);
    uint32_t game_lba, game_len;
    if (!root || find_entry(root, root_len, "PSP_GAME", 1, &game_lba, &game_len) != 0) {
        free(root);
        VT_ERR(err, errlen, "PSP_GAME folder not found (not a PSP game)");
        return -1;
    }
    free(root);

    uint8_t *game = read_extent(f, game_lba, game_len);
    uint32_t icon_lba, icon_len;
    if (!game || find_entry(game, game_len, "ICON0.PNG", 0, &icon_lba, &icon_len) != 0) {
        free(game);
        VT_ERR(err, errlen, "PSP_GAME/ICON0.PNG not found");
        return -1;
    }
    free(game);
    if (icon_len == 0 || icon_len > 1024 * 1024) { VT_ERR(err, errlen, "unexpected ICON0.PNG size"); return -1; }
    *offset = (uint64_t)icon_lba * ISO_SECTOR;
    *length = icon_len;
    return 0;
}
