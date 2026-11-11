/* Shared helpers: error reporting and small file utilities. */
#ifndef VT_H
#define VT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Writes a printf-style message into the caller's error buffer, if any. */
#define VT_ERR(err, n, ...) \
    do { if ((err) && (n) > 0) snprintf((err), (n), __VA_ARGS__); } while (0)

/* Reads a whole file into a malloc'd buffer. Fails if it is larger than max_len. */
int fs_read_all(const char *path, uint8_t **buf, size_t *len, size_t max_len);
/* Creates or truncates path and writes len bytes. */
int fs_write_all(const char *path, const void *buf, size_t len);
int fs_exists(const char *path);
int fs_remove(const char *path);
/* Creates every directory component of path (like mkdir -p). */
int fs_mkdirs(const char *path);
int fs_copy(const char *from, const char *to);
/* Creates the parent directories of a file path. */
int fs_mkdirs_for(const char *file_path);

#endif
