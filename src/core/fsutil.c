#include "vt.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int fs_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int fs_remove(const char *path)
{
    return remove(path);
}

int fs_read_all(const char *path, uint8_t **buf, size_t *len, size_t max_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    if (size < 0 || (size_t)size > max_len || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    uint8_t *data = malloc(size > 0 ? (size_t)size : 1);
    if (!data) { fclose(f); return -1; }
    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(data); return -1; }
    *buf = data;
    *len = got;
    return 0;
}

int fs_write_all(const char *path, const void *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t put = len ? fwrite(buf, 1, len, f) : 0;
    int closed = fclose(f);
    return (put == len && closed == 0) ? 0 : -1;
}

int fs_mkdirs(const char *path)
{
    char tmp[512];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof tmp) return -1;
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        /* Errors are ignored here: "ux0:" style prefixes and existing dirs fail harmlessly. */
        mkdir(tmp, 0777);
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
        struct stat st;
        if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    }
    return 0;
}

int fs_mkdirs_for(const char *file_path)
{
    char tmp[512];
    size_t n = strlen(file_path);
    if (n == 0 || n >= sizeof tmp) return -1;
    memcpy(tmp, file_path, n + 1);
    char *slash = strrchr(tmp, '/');
    if (!slash) return 0;
    *slash = '\0';
    return fs_mkdirs(tmp);
}

int fs_copy(const char *from, const char *to)
{
    uint8_t *data;
    size_t len;
    /* 64 MiB covers app.db and every icon with a wide margin. */
    if (fs_read_all(from, &data, &len, 64u * 1024 * 1024) != 0) return -1;
    int rc = fs_write_all(to, data, len);
    free(data);
    return rc;
}
