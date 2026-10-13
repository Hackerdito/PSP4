// disc.c - ISO / CSO (CISO v1) reader with a minimal ISO9660 walker.
#include "disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Raw-deflate decoder from stb_image (compiled in gfx.c).
extern int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

#define SECTOR 2048

struct Disc {
    FILE *f;
    DiscFormat fmt;
    uint64_t size;
    // CSO
    uint32_t block_size;
    uint32_t align;
    uint32_t nblocks;
    uint32_t *index;
    uint8_t *cbuf;      // compressed block scratch
    uint8_t *bbuf;      // decompressed block cache
    int64_t cached_block;
};

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)path[n - e + i]) != ext[i]) return 0;
    return 1;
}

Disc *disc_open(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    Disc *d = calloc(1, sizeof(Disc));
    d->f = f;
    d->cached_block = -1;

    uint8_t hdr[24];
    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr) goto fail;

    if (memcmp(hdr, "CISO", 4) == 0) {
        d->fmt = DISC_CSO;
        d->size = (uint64_t)rd32(hdr + 8) | ((uint64_t)rd32(hdr + 12) << 32);
        d->block_size = rd32(hdr + 16);
        d->align = hdr[21];
        if (d->block_size == 0 || d->block_size > (1 << 20)) goto fail;
        d->nblocks = (uint32_t)((d->size + d->block_size - 1) / d->block_size);
        d->index = malloc(((size_t)d->nblocks + 1) * 4);
        uint32_t hsize = rd32(hdr + 4);
        if (hsize < 24) hsize = 24;
        fseek(f, hsize, SEEK_SET);
        uint8_t *raw = malloc(((size_t)d->nblocks + 1) * 4);
        if (fread(raw, 4, d->nblocks + 1, f) != d->nblocks + 1) { free(raw); goto fail; }
        for (uint32_t i = 0; i <= d->nblocks; i++) d->index[i] = rd32(raw + i * 4);
        free(raw);
        d->cbuf = malloc(d->block_size * 2 + 64);
        d->bbuf = malloc(d->block_size);
    } else if (has_ext(path, ".iso")) {
        d->fmt = DISC_ISO;
        fseek(f, 0, SEEK_END);
        d->size = (uint64_t)ftello(f);
    } else {
        goto fail;
    }
    return d;
fail:
    disc_close(d);
    return NULL;
}

void disc_close(Disc *d) {
    if (!d) return;
    if (d->f) fclose(d->f);
    free(d->index);
    free(d->cbuf);
    free(d->bbuf);
    free(d);
}

DiscFormat disc_format(const Disc *d) { return d->fmt; }
uint64_t disc_size(const Disc *d) { return d->size; }

static int cso_load_block(Disc *d, uint32_t b) {
    if (d->cached_block == (int64_t)b) return 1;
    if (b >= d->nblocks) return 0;
    uint32_t i0 = d->index[b], i1 = d->index[b + 1];
    int plain = (i0 & 0x80000000u) != 0;
    uint64_t off = (uint64_t)(i0 & 0x7FFFFFFFu) << d->align;
    uint64_t end = (uint64_t)(i1 & 0x7FFFFFFFu) << d->align;
    size_t clen = end > off ? (size_t)(end - off) : 0;
    if (clen > d->block_size * 2 + 64) clen = d->block_size * 2 + 64;
    if (fseeko(d->f, (off_t)off, SEEK_SET) != 0) return 0;
    if (plain) {
        size_t n = fread(d->bbuf, 1, d->block_size, d->f);
        if (n < d->block_size) memset(d->bbuf + n, 0, d->block_size - n);
    } else {
        size_t n = fread(d->cbuf, 1, clen, d->f);
        int out = stbi_zlib_decode_noheader_buffer((char *)d->bbuf, d->block_size, (const char *)d->cbuf, (int)n);
        if (out < 0) return 0;
        if ((uint32_t)out < d->block_size) memset(d->bbuf + out, 0, d->block_size - out);
    }
    d->cached_block = b;
    return 1;
}

// Reads `len` bytes at byte offset `pos` of the uncompressed image.
static int disc_read(Disc *d, uint64_t pos, void *dst, size_t len) {
    if (pos + len > d->size) return 0;
    if (d->fmt == DISC_ISO) {
        if (fseeko(d->f, (off_t)pos, SEEK_SET) != 0) return 0;
        return fread(dst, 1, len, d->f) == len;
    }
    uint8_t *out = dst;
    while (len) {
        uint32_t b = (uint32_t)(pos / d->block_size);
        uint32_t o = (uint32_t)(pos % d->block_size);
        if (!cso_load_block(d, b)) return 0;
        size_t n = d->block_size - o;
        if (n > len) n = len;
        memcpy(out, d->bbuf + o, n);
        out += n; pos += n; len -= n;
    }
    return 1;
}

// Finds `name` inside the directory extent (lba,size). Fills its lba/size/flags.
static int iso_find(Disc *d, uint32_t lba, uint32_t size, const char *name,
                    uint32_t *out_lba, uint32_t *out_size, int *is_dir) {
    uint8_t sec[SECTOR];
    size_t nlen = strlen(name);
    for (uint32_t off = 0; off < size; off += SECTOR) {
        if (!disc_read(d, (uint64_t)(lba + off / SECTOR) * SECTOR, sec, SECTOR)) return 0;
        uint32_t p = 0;
        while (p < SECTOR) {
            uint8_t rlen = sec[p];
            if (rlen == 0) break;              // rest of sector is padding
            if (p + rlen > SECTOR || rlen < 34) break;
            const uint8_t *r = sec + p;
            uint8_t fl = r[32];
            const char *fn = (const char *)r + 33;
            size_t cmp = fl;
            for (size_t i = 0; i < fl; i++) if (fn[i] == ';') { cmp = i; break; }
            if (cmp == nlen) {
                int eq = 1;
                for (size_t i = 0; i < nlen; i++)
                    if (toupper((unsigned char)fn[i]) != toupper((unsigned char)name[i])) { eq = 0; break; }
                if (eq) {
                    *out_lba = rd32(r + 2);
                    *out_size = rd32(r + 10);
                    *is_dir = (r[25] & 2) != 0;
                    return 1;
                }
            }
            p += rlen;
        }
    }
    return 0;
}

uint8_t *disc_read_file(Disc *d, const char *path, size_t *len, size_t max_len) {
    uint8_t pvd[SECTOR];
    if (!disc_read(d, 16ull * SECTOR, pvd, SECTOR)) return NULL;
    if (pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return NULL;
    const uint8_t *root = pvd + 156;
    uint32_t lba = rd32(root + 2), size = rd32(root + 10);
    int is_dir = 1;

    char part[64];
    const char *p = path;
    while (*p) {
        const char *s = strchr(p, '/');
        size_t n = s ? (size_t)(s - p) : strlen(p);
        if (n == 0 || n >= sizeof part) return NULL;
        memcpy(part, p, n); part[n] = 0;
        if (!is_dir) return NULL;
        if (!iso_find(d, lba, size, part, &lba, &size, &is_dir)) return NULL;
        p += n + (s ? 1 : 0);
    }
    if (is_dir || size == 0 || size > max_len) return NULL;
    uint8_t *buf = malloc(size);
    if (!disc_read(d, (uint64_t)lba * SECTOR, buf, size)) { free(buf); return NULL; }
    *len = size;
    return buf;
}

int sfo_get_string(const uint8_t *sfo, size_t len, const char *key, char *out, size_t out_len) {
    if (len < 20 || memcmp(sfo, "\0PSF", 4) != 0) return 0;
    uint32_t keys = rd32(sfo + 8), data = rd32(sfo + 12), n = rd32(sfo + 16);
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = sfo + 20 + i * 16;
        if ((size_t)(e - sfo) + 16 > len) return 0;
        uint16_t koff = rd16(e), fmt = rd16(e + 2);
        uint32_t vlen = rd32(e + 4), doff = rd32(e + 12);
        if (keys + koff >= len) return 0;
        if (strcmp((const char *)sfo + keys + koff, key) != 0) continue;
        if (fmt != 0x0204 && fmt != 0x0004) return 0;   // utf8 string
        if (data + doff + vlen > len) return 0;
        size_t c = vlen < out_len - 1 ? vlen : out_len - 1;
        memcpy(out, sfo + data + doff, c);
        out[c] = 0;
        return 1;
    }
    return 0;
}
