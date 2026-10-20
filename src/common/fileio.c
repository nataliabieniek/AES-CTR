#include "fileio.h"
#include <stdlib.h>

#if defined(__MINGW32__)
#define SEEK64(f, off, wh) fseeko64((f), (off64_t)(off), (wh))
#define TELL64(f) ((int64_t)ftello64(f))
#elif defined(_WIN32)
#define SEEK64(f, off, wh) _fseeki64((f), (long long)(off), (wh))
#define TELL64(f) ((int64_t)_ftelli64(f))
#else
#define SEEK64(f, off, wh) fseeko((f), (off_t)(off), (wh))
#define TELL64(f) ((int64_t)ftello(f))
#endif

int64_t file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int64_t sz = -1;
    if (SEEK64(f, 0, SEEK_END) == 0) sz = TELL64(f);
    fclose(f);
    return sz;
}

FILE *file_open(const char *path, const char *mode) {
    FILE *f = fopen(path, mode);
    if (!f) perror(path);
    return f;
}

size_t file_read_at(FILE *f, uint64_t offset, uint8_t *buf, size_t n) {
    if (SEEK64(f, offset, SEEK_SET) != 0) return 0;
    return fread(buf, 1, n, f);
}

size_t file_write_at(FILE *f, uint64_t offset, const uint8_t *buf, size_t n) {
    if (SEEK64(f, offset, SEEK_SET) != 0) return 0;
    return fwrite(buf, 1, n, f);
}

uint8_t *file_read_all(const char *path, size_t *len) {
    int64_t sz = file_size(path);
    if (sz < 0) { perror(path); return NULL; }
    FILE *f = file_open(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = malloc(sz ? (size_t)sz : 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

int file_write_all(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = file_open(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(buf, 1, len, f);
    return (fclose(f) == 0 && w == len) ? 0 : -1;
}
