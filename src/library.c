// library.c - folder scan + metadata extraction.
#include "library.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef VAULT_HOST
const char *const kGameDirs[] = { "testdata/data/vault/PSP/GAME", "testdata/usb0/vault/PSP/GAME", NULL };
#else
// M33's own games folder comes first: games there show up in both apps.
const char *const kGameDirs[] = { "/data/March33/games", "/data/vault/PSP/GAME", "/mnt/usb0/vault/PSP/GAME",
                                  "/mnt/usb1/vault/PSP/GAME", "/mnt/usb0/PSP/GAME", "/mnt/usb1/PSP/GAME", NULL };
#endif
#ifdef VAULT_HOST
const char *const kGameDirLabels[] = { "Interno", "USB0", NULL };
#else
const char *const kGameDirLabels[] = { "Interno", "Interno", "USB0", "USB1", "USB0", "USB1", NULL };
#endif

static int ends_with_ci(const char *s, const char *ext) {
    size_t n = strlen(s), e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)s[n - e + i]) != ext[i]) return 0;
    return 1;
}

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

void format_size(uint64_t b, char *out, int n) {
    if (b >= (1ull << 30)) snprintf(out, n, "%.1f GB", b / 1073741824.0);
    else snprintf(out, n, "%llu MB", (unsigned long long)(b >> 20));
}

// A title is usable if most of its characters are Latin (our fonts cover Latin-1).
static int title_is_latin(const char *s) {
    int latin = 0, other = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p;) {
        if (*p < 0x80) { latin++; p++; }
        else if ((*p & 0xE0) == 0xC0) { latin++; p += 2; }   // 2-byte = Latin-1 supplement etc.
        else { other++; p += ((*p & 0xF0) == 0xE0) ? 3 : 4; }
    }
    return latin > 0 && other * 3 < latin;
}

static void title_from_file(const char *file, char *out, int n) {
    snprintf(out, n, "%s", file);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
    for (char *p = out; *p; p++) if (*p == '_' || *p == '.') *p = ' ';
}

static uint32_t hash_str(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static uint32_t hsv(float h, float s, float v) {
    float r, g, b, f = h * 6; int i = (int)f % 6; f -= (int)f;
    float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break;
                 case 2: r = p; g = v; b = t; break; case 3: r = p; g = q; b = v; break;
                 case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    return RGB((int)(r * 255), (int)(g * 255), (int)(b * 255));
}

static void load_meta(Game *g, int tile_w, int tile_h) {
    Disc *d = disc_open(g->path);
    uint32_t h = hash_str(g->file);
    float hue = (h % 360) / 360.0f;
    g->c1 = hsv(hue, 0.65f, 0.95f);
    g->c2 = hsv(hue + 0.12f > 1 ? hue - 0.88f : hue + 0.12f, 0.75f, 0.85f);
    title_from_file(g->file, g->title, sizeof g->title);
    if (!d) return;
    g->fmt = disc_format(d);
    g->image_size = disc_size(d);

    size_t len;
    uint8_t *sfo = disc_read_file(d, "PSP_GAME/PARAM.SFO", &len, 64 * 1024);
    if (sfo) {
        char t[160];
        g->is_psp = 1;
        if (sfo_get_string(sfo, len, "TITLE", t, sizeof t) && title_is_latin(t)) {
            // Titles sometimes carry line breaks.
            for (char *p = t; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
            snprintf(g->title, sizeof g->title, "%s", t);
        }
        sfo_get_string(sfo, len, "DISC_ID", g->disc_id, sizeof g->disc_id);
        free(sfo);
    }
    uint8_t *png = disc_read_file(d, "PSP_GAME/ICON0.PNG", &len, 512 * 1024);
    if (png) {
        g->icon = image_decode(png, (int)len);
        free(png);
        if (g->icon.px) {
            g->tile = image_scale_cover(&g->icon, tile_w, tile_h);
            image_palette(&g->icon, &g->c1, &g->c2);
        }
    }
    disc_close(d);
}

static int cmp_games(const void *a, const void *b) {
    const Game *x = a, *y = b;
    return strcasecmp(x->title, y->title);
}

void library_scan(Library *lib, int tile_w, int tile_h) {
    library_free(lib);
    mkdirs(kGameDirs[0]);
    for (int k = 0; kGameDirs[k]; k++) {
        DIR *dir = opendir(kGameDirs[k]);
        if (!dir) continue;
        lib->folders_found++;
        struct dirent *e;
        while ((e = readdir(dir)) && lib->count < MAX_GAMES) {
            if (e->d_name[0] == '.') continue;
            if (!ends_with_ci(e->d_name, ".iso") && !ends_with_ci(e->d_name, ".cso")) continue;
            Game *g = &lib->games[lib->count];
            memset(g, 0, sizeof *g);
            snprintf(g->path, sizeof g->path, "%s/%s", kGameDirs[k], e->d_name);
            snprintf(g->file, sizeof g->file, "%s", e->d_name);
            snprintf(g->source, sizeof g->source, "%s", kGameDirLabels[k]);
            struct stat st;
            if (stat(g->path, &st) == 0) g->file_size = (uint64_t)st.st_size;
            g->fmt = ends_with_ci(e->d_name, ".cso") ? DISC_CSO : DISC_ISO;
            load_meta(g, tile_w, tile_h);
            if (!g->image_size) g->image_size = g->file_size;
            lib->total_bytes += g->file_size;
            lib->count++;
        }
        closedir(dir);
    }
    qsort(lib->games, lib->count, sizeof(Game), cmp_games);
}

void library_free(Library *lib) {
    for (int i = 0; i < lib->count; i++) {
        image_free(&lib->games[i].tile);
        image_free(&lib->games[i].icon);
    }
    lib->count = 0;
    lib->total_bytes = 0;
    lib->folders_found = 0;
}

Image library_load_cover(const Game *g, int w, int h) {
    Image out = { 0 };
    Disc *d = disc_open(g->path);
    if (!d) return out;
    size_t len;
    uint8_t *png = disc_read_file(d, "PSP_GAME/PIC1.PNG", &len, 2 * 1024 * 1024);
    if (png) {
        Image src = image_decode(png, (int)len);
        free(png);
        if (src.px) { out = image_scale_cover(&src, w, h); image_free(&src); }
    }
    disc_close(d);
    if (!out.px && g->icon.px) out = image_scale_cover(&g->icon, w, h);
    return out;
}
