// disc.c - ISO / CSO (CISO v1) reader with a minimal ISO9660 walker.
#include "disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

// Raw-deflate decoder (stb_image, wrapped in gfx.c).
extern int vault_inflate_raw(char *out, int out_len, const char *in, int in_len);

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
    // Raw CD images (.bin / .cue): 2352-byte sectors, user data at raw_off (24 mode 2, 16 mode 1).
    uint32_t raw_sector;
    uint32_t raw_off;
    uint64_t raw_base;  // byte offset of the track in the file
    uint64_t file_bytes;
};

uint64_t disc_file_bytes(const Disc *d) { return d->file_bytes; }

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

static int has_ext(const char *path, const char *ext) {
    size_t n = strlen(path), e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)path[n - e + i]) != ext[i]) return 0;
    return 1;
}

static void raw_setup(Disc *d, uint8_t mode) {
    d->file_bytes = d->size;
    d->raw_sector = 2352;
    d->raw_off = mode == 1 ? 16 : 24;
    d->size = d->size / 2352 * 2048;   // as if it were a plain 2048-byte ISO
}

// A cue sheet's first FILE "x.bin" line, resolved next to the .cue.
static int cue_first_file(FILE *f, const char *cue, char *out, size_t out_len) {
    char line[512];
    fseek(f, 0, SEEK_SET);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncasecmp(p, "FILE", 4) != 0) continue;
        p += 4;
        while (*p == ' ' || *p == '\t') p++;
        char name[300];
        size_t n = 0;
        if (*p == '"') { p++; while (*p && *p != '"' && n < sizeof name - 1) name[n++] = *p++; }
        else while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && n < sizeof name - 1) name[n++] = *p++;
        name[n] = 0;
        if (!n) return 0;
        const char *slash = strrchr(cue, '/');
        if (slash) snprintf(out, out_len, "%.*s/%s", (int)(slash - cue), cue, name);
        else snprintf(out, out_len, "%s", name);
        FILE *t = fopen(out, "rb");
        if (!t) {   // names in cue sheets often differ in case from the file on a FAT drive
            return 0;
        }
        fclose(t);
        return 1;
    }
    return 0;
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
    } else if (has_ext(path, ".iso") || has_ext(path, ".bin") || has_ext(path, ".img")) {
        d->fmt = has_ext(path, ".bin") ? DISC_BIN : DISC_ISO;
        fseek(f, 0, SEEK_END);
        d->size = (uint64_t)ftello(f);
        static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        if (memcmp(hdr, sync, 12) == 0) raw_setup(d, hdr[15]);   // raw sectors (PS1 dumps)
    } else if (has_ext(path, ".cue")) {
        char bin[512];
        if (!cue_first_file(f, path, bin, sizeof bin)) goto fail;
        fclose(f);
        d->f = f = fopen(bin, "rb");
        if (!f) goto fail;
        d->fmt = DISC_CUE;
        fseek(f, 0, SEEK_END);
        d->size = (uint64_t)ftello(f);
        uint8_t h2[16];
        fseek(f, 0, SEEK_SET);
        if (fread(h2, 1, 16, f) != 16) goto fail;
        raw_setup(d, h2[15]);
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
        int out = vault_inflate_raw((char *)d->bbuf, d->block_size, (const char *)d->cbuf, (int)n);
        if (out < 0) return 0;
        if ((uint32_t)out < d->block_size) memset(d->bbuf + out, 0, d->block_size - out);
    }
    d->cached_block = b;
    return 1;
}

// Reads `len` bytes at byte offset `pos` of the uncompressed image.
static int disc_read(Disc *d, uint64_t pos, void *dst, size_t len) {
    if (pos + len > d->size) return 0;
    if (d->raw_sector) {
        uint8_t *out = dst;
        while (len) {
            uint64_t sec = pos / SECTOR;
            uint32_t o = (uint32_t)(pos % SECTOR);
            size_t n = SECTOR - o;
            if (n > len) n = len;
            if (fseeko(d->f, (off_t)(d->raw_base + sec * d->raw_sector + d->raw_off + o), SEEK_SET) != 0) return 0;
            if (fread(out, 1, n, d->f) != n) return 0;
            out += n; pos += n; len -= n;
        }
        return 1;
    }
    if (d->fmt == DISC_ISO || d->fmt == DISC_BIN || d->fmt == DISC_CUE) {
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

// ---------------------------------------------------------------- formats / EBOOT.PBP

int disc_format_from_name(const char *name) {
    if (has_ext(name, ".iso")) return DISC_ISO;
    if (has_ext(name, ".cso")) return DISC_CSO;
    if (has_ext(name, ".pbp")) return DISC_PBP;
    if (has_ext(name, ".chd")) return DISC_CHD;
    if (has_ext(name, ".cue")) return DISC_CUE;
    if (has_ext(name, ".bin")) return DISC_BIN;
    return -1;
}

const char *disc_format_name(DiscFormat f) {
    switch (f) {
    case DISC_CSO: return "CSO";
    case DISC_PBP: return "PBP";
    case DISC_CHD: return "CHD";
    case DISC_CUE: return "CUE";
    case DISC_BIN: return "BIN";
    default: return "ISO";
    }
}

// Header: "\0PBP", version, then 8 entry offsets; each entry runs to the next offset (the last
// one to the end of the file).
static int pbp_entry_span(FILE *f, int entry, uint32_t *off, uint32_t *len) {
    uint8_t h[40];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, sizeof h, f) != sizeof h) return 0;
    if (memcmp(h, "\0PBP", 4) != 0) return 0;
    uint32_t start = rd32(h + 8 + entry * 4), end;
    if (entry < 7) end = rd32(h + 8 + (entry + 1) * 4);
    else { fseek(f, 0, SEEK_END); end = (uint32_t)ftell(f); }
    if (end < start) return 0;
    *off = start; *len = end - start;
    return 1;
}

uint8_t *pbp_read_entry(const char *path, int entry, size_t *len, size_t max_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint32_t off, n;
    uint8_t *buf = NULL;
    if (pbp_entry_span(f, entry, &off, &n) && n > 0 && n <= max_len) {
        buf = malloc(n);
        if (fseek(f, off, SEEK_SET) != 0 || fread(buf, 1, n, f) != n) { free(buf); buf = NULL; }
        else *len = n;
    }
    fclose(f);
    return buf;
}

int pbp_is_ps1(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t off, n;
    int ps1 = 0;
    char magic[12] = { 0 };
    if (pbp_entry_span(f, PBP_PSAR, &off, &n) && n >= sizeof magic && fseek(f, off, SEEK_SET) == 0 &&
        fread(magic, 1, sizeof magic, f) == sizeof magic)
        ps1 = memcmp(magic, "PSISOIMG", 8) == 0 || memcmp(magic, "PSTITLEIMG", 10) == 0;
    fclose(f);
    if (!ps1) {   // or the SFO says so (CATEGORY "ME")
        size_t len;
        uint8_t *sfo = pbp_read_entry(path, PBP_SFO, &len, 64 * 1024);
        char cat[8];
        if (sfo && sfo_get_string(sfo, len, "CATEGORY", cat, sizeof cat) && strcmp(cat, "ME") == 0) ps1 = 1;
        free(sfo);
    }
    return ps1;
}

// ---------------------------------------------------------------- PS1 discs / CHD

int ps1_disc_id(Disc *d, char *out, size_t out_len) {
    size_t len;
    uint8_t *cnf = disc_read_file(d, "SYSTEM.CNF", &len, 4096);
    if (!cnf) return 0;
    char text[4097];
    memcpy(text, cnf, len);
    text[len] = 0;
    free(cnf);
    // BOOT = cdrom:\SLUS_005.94;1  ->  SLUS00594
    char *b = strstr(text, "BOOT");
    if (!b) { if (out_len) out[0] = 0; return 1; }
    char *p = strchr(b, ':');
    p = p ? p + 1 : b + 4;
    char *end = p;
    while (*end && *end != ';' && *end != '\r' && *end != '\n') end++;
    for (char *q = p; q < end; q++) if (*q == '\\' || *q == '/') p = q + 1;   // last path part
    size_t n = 0;
    for (; p < end && n + 1 < out_len; p++)
        if (isalnum((unsigned char)*p)) out[n++] = (char)toupper((unsigned char)*p);
    out[n] = 0;
    return 1;
}

static uint64_t rd64be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

int chd_is_cd(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t h[124];
    int cd = 0;
    if (fread(h, 1, sizeof h, f) == sizeof h && memcmp(h, "MComprHD", 8) == 0) {
        uint32_t version = (uint32_t)h[12] << 24 | h[13] << 16 | h[14] << 8 | h[15];
        uint64_t meta = version >= 5 ? rd64be(h + 48) : version >= 3 ? rd64be(h + 36) : 0;
        for (int guard = 0; meta && guard < 64; guard++) {
            uint8_t e[16];
            if (fseeko(f, (off_t)meta, SEEK_SET) != 0 || fread(e, 1, 16, f) != 16) break;
            if (!memcmp(e, "CHT2", 4) || !memcmp(e, "CHTR", 4) || !memcmp(e, "CHCD", 4) || !memcmp(e, "CHGD", 4)) { cd = 1; break; }
            if (!memcmp(e, "DVD ", 4)) break;
            meta = rd64be(e + 8);
        }
    }
    fclose(f);
    return cd;
}
