// library.c - folder scan + metadata extraction.
#include "library.h"
#include "browser.h"

#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Where VAULT looks. Internal folders are always reported; a USB port only when a drive is in it.
// USB drives are searched whole (4 folders deep, enough for PSP/GAME/<id>/EBOOT.PBP), so games can sit anywhere on them.
#ifdef VAULT_HOST
const GameDir kGameDirs[] = {
    { "testdata/data/March33/games", "Interno", 2, 0 },
    { "testdata/data/vault/PSP/GAME", "Interno", 2, 0 },
    { "testdata/usb0", "USB0", 4, 1 },
    { "testdata/usb1", "USB1", 4, 1 },
    { NULL, NULL, 0, 0 },
};
#else
const GameDir kGameDirs[] = {
    { "/data/vault/games", "Interno", 2, 0 },     // VAULT's games folder (the engine's browser starts here too)
    { "/data/vault/PSP/GAME", "Interno", 2, 0 },
    { "/data/March33/games", "Interno", 2, 1 },   // M33's folder, if it's installed too
    { "/mnt/usb0", "USB0", 4, 1 }, { "/mnt/usb1", "USB1", 4, 1 },
    { "/mnt/usb2", "USB2", 4, 1 }, { "/mnt/usb3", "USB3", 4, 1 },
    { "/mnt/usb4", "USB4", 4, 1 }, { "/mnt/usb5", "USB5", 4, 1 },
    { "/mnt/usb6", "USB6", 4, 1 }, { "/mnt/usb7", "USB7", 4, 1 },
    { NULL, NULL, 0, 0 },
};
#endif

static int skip_folder(const char *name) {
    return strcasecmp(name, "System Volume Information") == 0 || strcasecmp(name, "$RECYCLE.BIN") == 0 ||
           strcasecmp(name, "LOST.DIR") == 0;
}

int path_exists(const char *path) { return access(path, F_OK) == 0; }

int path_is_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return 0;
    closedir(d);
    return 1;
}

uint64_t path_size(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    off_t end = lseek(fd, 0, SEEK_END);
    close(fd);
    return end > 0 ? (uint64_t)end : 0;
}

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
    else if (b >= (1ull << 20)) snprintf(out, n, "%llu MB", (unsigned long long)(b >> 20));
    else snprintf(out, n, "%llu KB", (unsigned long long)((b + 1023) >> 10));
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

static void apply_sfo(Game *g, uint8_t *sfo, size_t len) {
    char t[160];
    g->is_psp = 1;
    if (sfo_get_string(sfo, len, "TITLE", t, sizeof t) && title_is_latin(t)) {
        // Titles sometimes carry line breaks.
        for (char *p = t; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
        snprintf(g->title, sizeof g->title, "%s", t);
    }
    if (!sfo_get_string(sfo, len, "DISC_ID", g->disc_id, sizeof g->disc_id))
        sfo_get_string(sfo, len, "TITLE_ID", g->disc_id, sizeof g->disc_id);
}

static void apply_icon(Game *g, uint8_t *png, size_t len, int tile_w, int tile_h) {
    g->icon = image_decode(png, (int)len);
    if (g->icon.px) {
        g->tile = image_scale_cover(&g->icon, tile_w, tile_h);
        image_palette(&g->icon, &g->c1, &g->c2);
    }
}

static void load_meta(Game *g, int tile_w, int tile_h) {
    uint32_t h = hash_str(g->file);
    float hue = (h % 360) / 360.0f;
    g->c1 = hsv(hue, 0.65f, 0.95f);
    g->c2 = hsv(hue + 0.12f > 1 ? hue - 0.88f : hue + 0.12f, 0.75f, 0.85f);
    title_from_file(g->file, g->title, sizeof g->title);
    // "EBOOT.PBP" says nothing: name the game after its folder until the SFO gives the title.
    if (strcasecmp(g->file, "EBOOT.PBP") == 0) {
        char dir[512];
        snprintf(dir, sizeof dir, "%s", g->path);
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = 0; slash = strrchr(dir, '/'); }
        if (slash) title_from_file(slash + 1, g->title, sizeof g->title);
    }
    size_t len;

    if (g->fmt == DISC_PBP) {
        g->image_size = g->file_size;
        uint8_t *sfo = pbp_read_entry(g->path, PBP_SFO, &len, 64 * 1024);
        if (sfo) { apply_sfo(g, sfo, len); free(sfo); }
        g->is_ps1 = pbp_is_ps1(g->path);
        uint8_t *png = pbp_read_entry(g->path, PBP_ICON0, &len, 512 * 1024);
        if (png) { apply_icon(g, png, len, tile_w, tile_h); free(png); }
        return;
    }
    if (g->fmt == DISC_CHD) {   // compressed hunks (zlib/lzma/flac/zstd): shown by file name
        g->image_size = g->file_size;
        g->is_ps1 = chd_is_cd(g->path);
        g->is_psp = !g->is_ps1;
        return;
    }

    Disc *d = disc_open(g->path);
    if (!d) return;
    g->fmt = disc_format(d);
    g->image_size = disc_size(d);
    if (disc_file_bytes(d) > g->file_size) g->file_size = disc_file_bytes(d);   // a .cue's .bin
    uint8_t *sfo = NULL;
    if (g->fmt != DISC_CUE && g->fmt != DISC_BIN) sfo = disc_read_file(d, "PSP_GAME/PARAM.SFO", &len, 64 * 1024);
    if (sfo) {
        apply_sfo(g, sfo, len);
        free(sfo);
        uint8_t *png = disc_read_file(d, "PSP_GAME/ICON0.PNG", &len, 512 * 1024);
        if (png) { apply_icon(g, png, len, tile_w, tile_h); free(png); }
    } else if (ps1_disc_id(d, g->disc_id, sizeof g->disc_id) || g->fmt == DISC_CUE || g->fmt == DISC_BIN) {
        g->is_ps1 = 1;   // no PSP_GAME, but SYSTEM.CNF (or a raw CD image): PlayStation 1
    }
    disc_close(d);
}

static int cmp_games(const void *a, const void *b) {
    const Game *x = a, *y = b;
    return strcasecmp(x->title, y->title);
}

// Resolves `path` component by component, matching names case-insensitively
// ("/data/march33/Games" finds "/data/March33/games"). Returns 1 and fills out if it exists.
static int resolve_ci(const char *path, char *out, size_t out_len) {
    char cur[512] = "";
    const char *p = path;
    if (*p == '/') { snprintf(cur, sizeof cur, "/"); p++; }
    while (*p) {
        const char *s = strchr(p, '/');
        size_t n = s ? (size_t)(s - p) : strlen(p);
        char part[256];
        snprintf(part, sizeof part, "%.*s", (int)n, p);
        char exact[512];
        snprintf(exact, sizeof exact, "%s%s%s", cur, (cur[0] && cur[strlen(cur) - 1] != '/') ? "/" : "", part);
        if (path_exists(exact)) {
            snprintf(cur, sizeof cur, "%s", exact);
        } else {
            DIR *d = opendir(cur[0] ? cur : ".");
            int found = 0;
            if (d) {
                struct dirent *e;
                while ((e = readdir(d))) {
                    if (strcasecmp(e->d_name, part) == 0) {
                        char next[512];
                        snprintf(next, sizeof next, "%s%s%s", cur, (cur[0] && cur[strlen(cur) - 1] != '/') ? "/" : "", e->d_name);
                        snprintf(cur, sizeof cur, "%s", next);
                        found = 1;
                        break;
                    }
                }
                closedir(d);
            }
            if (!found) return 0;
        }
        p += n + (s ? 1 : 0);
    }
    snprintf(out, out_len, "%s", cur);
    return 1;
}

typedef struct { char ext[8]; int n; } ExtCount;

static void count_ext(ExtCount *ec, int *nec, const char *name) {
    const char *dot = strrchr(name, '.');
    char ext[8];
    snprintf(ext, sizeof ext, "%s", dot ? dot : "(sin)");
    for (char *q = ext; *q; q++) *q = (char)tolower((unsigned char)*q);
    for (int i = 0; i < *nec; i++) if (strcmp(ec[i].ext, ext) == 0) { ec[i].n++; return; }
    if (*nec < 8) { snprintf(ec[*nec].ext, sizeof ec[*nec].ext, "%s", ext); ec[*nec].n = 1; (*nec)++; }
}

static FILE *g_log;

static int folder_has_cue(const char *dirpath) {
    DIR *d = opendir(dirpath);
    if (!d) return 0;
    struct dirent *e;
    int found = 0;
    while (!found && (e = readdir(d))) found = disc_format_from_name(e->d_name) == DISC_CUE;
    closedir(d);
    return found;
}

static void scan_dir(Library *lib, const char *dirpath, const char *label, int depth, int max_depth,
                     DirReport *r, ExtCount *ec, int *nec, int tile_w, int tile_h) {
    DIR *dir = opendir(dirpath);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) && lib->count < MAX_GAMES) {
        if (e->d_name[0] == '.') continue;
        char full[512];
        snprintf(full, sizeof full, "%s/%s", dirpath, e->d_name);
        if (path_is_dir(full)) {
            if (skip_folder(e->d_name)) continue;
            r->subdirs++;
            if (g_log) fprintf(g_log, "  [dir] %s\n", full);
            // Games are often kept a folder or two deep ("games/God of War/god.iso", "USB:/ISO/god.iso").
            if (depth < max_depth) scan_dir(lib, full, label, depth + 1, max_depth, r, ec, nec, tile_w, tile_h);
            continue;
        }
        r->files++;
        int fmt = disc_format_from_name(e->d_name);
        // PARAM.PBP is a game update's data, not a game.
        int is_game = fmt >= 0 && strcasecmp(e->d_name, "PARAM.PBP") != 0;
        // A .bin with a .cue next to it is one of the cue's tracks: the .cue is the game.
        if (fmt == DISC_BIN && folder_has_cue(dirpath)) is_game = 0;
        if (g_log) fprintf(g_log, "  %s %s (%lld bytes)\n", is_game ? "[juego]" : "[otro] ", full, (long long)path_size(full));
        if (!is_game) {
            count_ext(ec, nec, e->d_name);
            if (!r->sample[0]) snprintf(r->sample, sizeof r->sample, "%s", e->d_name);
            continue;
        }
        int dup = 0;   // same file reached through two folders (a saved folder inside a USB)
        for (int i = 0; i < lib->count; i++) if (strcmp(lib->games[i].path, full) == 0) { dup = 1; break; }
        if (dup) continue;
        r->games++;
        Game *g = &lib->games[lib->count];
        memset(g, 0, sizeof *g);
        snprintf(g->path, sizeof g->path, "%s", full);
        snprintf(g->file, sizeof g->file, "%s", e->d_name);
        snprintf(g->source, sizeof g->source, "%s", label);
        g->file_size = path_size(full);
        g->fmt = (DiscFormat)fmt;
        load_meta(g, tile_w, tile_h);
        g->sys = g->is_ps1 ? SYS_PS1 : g->fmt == DISC_PBP ? SYS_EBOOT : SYS_PSP;
        if (!g->image_size) g->image_size = g->file_size;
        lib->total_bytes += g->file_size;
        lib->count++;
    }
    closedir(dir);
}

void library_scan(Library *lib, int tile_w, int tile_h) {
    library_free(lib);
#ifdef VAULT_HOST
    const char *log_path = "build/host/scan.log";
#else
    const char *log_path = "/data/vault/scan.log";
    mkdirs("/data/vault");
#endif
    g_log = fopen(log_path, "w");
#ifdef VAULT_HOST
    DIR *data = opendir(getenv("VAULT_SANDBOXED") ? "/nonexistent" : ".");
#else
    DIR *data = opendir("/data");
#endif
    lib->data_readable = data != NULL;
    if (data) closedir(data);
    if (g_log) fprintf(g_log, "SAMPSP4 scan. /data legible: %s\n", lib->data_readable ? "si" : "NO (sandbox)");

    // Fixed folders, then the ones added from Explorar.
    GameDir dirs[48];
    int nd = 0;
    for (int k = 0; kGameDirs[k].path && nd < 16; k++) dirs[nd++] = kGameDirs[k];
    static char saved[32][512];
    int ns = library_folders_load(saved, 32);
    static char labels[32][16];
    for (int k = 0; k < ns && nd < 47; k++) {
        const char *u = strstr(saved[k], "usb");
        if (u && u[3] >= '0' && u[3] <= '9') snprintf(labels[k], sizeof labels[k], "USB%c", u[3]);
        else snprintf(labels[k], sizeof labels[k], "Mi carpeta");
        dirs[nd++] = (GameDir){ saved[k], labels[k], 3, 0 };
    }
    dirs[nd].path = NULL;
    for (int k = 0; dirs[k].path; k++) {
        const GameDir *gd = &dirs[k];
        DirReport r;
        memset(&r, 0, sizeof r);
        snprintf(r.path, sizeof r.path, "%s", gd->path);
        if (!resolve_ci(gd->path, r.used, sizeof r.used)) {
            r.err = errno;
            if (g_log) fprintf(g_log, "%s: no existe (errno %d)\n", gd->path, r.err);
        } else {
            // An empty USB port can still show as an empty folder: count it only if it can be listed.
            DIR *probe = opendir(r.used);
            if (probe) { r.exists = 1; closedir(probe); }
        }
        if (r.exists) {
            lib->folders_found++;
            if (g_log) fprintf(g_log, "%s -> %s\n", gd->path, r.used);
            ExtCount ec[8];
            int nec = 0;
            scan_dir(lib, r.used, gd->label, 0, gd->max_depth, &r, ec, &nec, tile_w, tile_h);
            char *o = r.other;
            size_t left = sizeof r.other;
            for (int i = 0; i < nec && left > 1; i++) {
                int w = snprintf(o, left, "%s%s x%d", i ? ", " : "", ec[i].ext, ec[i].n);
                if (w < 0 || (size_t)w >= left) break;
                o += w; left -= w;
            }
            if (g_log) fprintf(g_log, "  = %d juegos, %d otros archivos, %d carpetas\n", r.games, r.files - r.games, r.subdirs);
        }
        if ((r.exists || !gd->optional) && lib->report_count < MAX_REPORTS)
            lib->reports[lib->report_count++] = r;
    }
    if (g_log) { fclose(g_log); g_log = NULL; }
    // The first two folders are VAULT's / M33's own: create them if missing, after reporting.
    // (only when no folder with that name in any capitalisation exists, so a "Games" isn't shadowed).
    char tmp[256];
    for (int k = 0; kGameDirs[k].path; k++)
        if (!kGameDirs[k].optional && !resolve_ci(kGameDirs[k].path, tmp, sizeof tmp)) mkdirs(kGameDirs[k].path);
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
    lib->report_count = 0;
}

Image library_load_cover(const Game *g, int w, int h) {
    Image out = { 0 };
    size_t len;
    if (g->fmt == DISC_PBP || g->fmt == DISC_CHD) {
        uint8_t *png = g->fmt == DISC_PBP ? pbp_read_entry(g->path, PBP_PIC1, &len, 2 * 1024 * 1024) : NULL;
        if (png) {
            Image src = image_decode(png, (int)len);
            free(png);
            if (src.px) { out = image_scale_cover(&src, w, h); image_free(&src); }
        }
        if (!out.px && g->icon.px) out = image_scale_cover(&g->icon, w, h);
        return out;
    }
    Disc *d = disc_open(g->path);
    if (!d) return out;
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
