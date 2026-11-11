/* Just enough ISO9660 to find PSP_GAME/ICON0.PNG inside a PSP disc image. */
#ifndef VT_ISO9660_H
#define VT_ISO9660_H

#include <stdint.h>
#include <stdio.h>

#define ISO_SECTOR 2048

/*
 * Locates PSP_GAME/ICON0.PNG. On success sets the absolute byte offset and the
 * recorded data length. Returns 0 on success, -1 with a message in err otherwise.
 */
int iso_find_icon0(FILE *f, uint64_t *offset, uint32_t *length, char *err, size_t errlen);
/* Reads length bytes at offset. Returns 0 on success. */
int iso_read_at(FILE *f, uint64_t offset, void *buf, size_t length);
/* Overwrites length bytes at offset without changing the file size. Returns 0 on success. */
int iso_write_at(FILE *f, uint64_t offset, const void *buf, size_t length);

#endif
