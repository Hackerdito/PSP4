// browser.c - lists drives and folders for the Explorar window.
#include "browser.h"
#include "library.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct { const char *path; const char *name; const char *note; } Root;

#ifdef VAULT_HOST
static const Root kRoots[] = {
    { "testdata/usb0", "USB0", "Memoria USB" }, { "testdata/usb1", "USB1", "Memoria USB" },
    { "testdata/data/March33/games", "Juegos de M33", "Interno" },
    { "testdata/data", "Disco interno", "/data" }, { NULL, NULL, NULL },
};
#else
static const Root kRoots[] = {
    { "/mnt/usb0", "USB0", "Memoria USB" }, { "/mnt/usb1", "USB1", "Memoria USB" },
    { "/mnt/usb2", "USB2", "Memoria USB" }, { "/mnt/usb3", "USB3", "Memoria USB" },
    { "/mnt/usb4", "USB4", "Memoria USB" }, { "/mnt/usb5", "USB5", "Memoria USB" },
    { "/mnt/usb6", "USB6", "Memoria USB" }, { "/mnt/usb7", "USB7", "Memoria USB" },
    { "/data/March33/games", "Juegos de M33", "Interno" },
    { "/data", "Disco interno", "/data" },
    { "/", "Todo el sistema", "/" },
    { NULL, NULL, NULL },
};
#endif

static int ends_ci(const char *s, const char *ext) {
    size_t n = strlen(s), e = strlen(ext);
    if (n < e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)s[n - e + i]) != ext[i]) return 0;
    return 1;
}

void browser_display_path(const char *path, char *out, int out_len) {
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; kRoots[i].path; i++) {
        size_t l = strlen(kRoots[i].path);
        if (strcmp(kRoots[i].path, "/") == 0) continue;
        if (strncmp(path, kRoots[i].path, l) == 0 && (path[l] == 0 || path[l] == '/') && l > best_len) { best = i; best_len = l; }
    }
    char rest[512];
    const char *tail = best >= 0 ? path + best_len : path;
    while (*tail == '/') tail++;
    // Separate folders with " / " so the path reads like breadcrumbs.
    int o = 0;
    for (const char *p = tail; *p && o < (int)sizeof rest - 4; p++) {
        if (*p == '/') { rest[o++] = ' '; rest[o++] = '/'; rest[o++] = ' '; }
        else rest[o++] = *p;
    }
    rest[o] = 0;
    const char *head = best >= 0 ? kRoots[best].name : "Sistema";
    if (rest[0]) snprintf(out, out_len, "%s / %s", head, rest);
    else snprintf(out, out_len, "%s", head);
}

void browser_show_root(Browser *b) {
    b->at_root = 1;
    b->path[0] = 0;
    b->count = 0;
    b->sel = 0;
    b->scroll = 0;
    b->truncated = 0;
    b->error[0] = 0;
    for (int i = 0; kRoots[i].path; i++) {
        DIR *d = opendir(kRoots[i].path);
        if (!d) continue;   // empty port or no access
        int n = 0;
        struct dirent *e;
        while ((e = readdir(d)) && n < 999) if (e->d_name[0] != '.') n++;
        closedir(d);
        BrowserEntry *en = &b->entries[b->count++];
        memset(en, 0, sizeof *en);
        en->kind = ENTRY_DRIVE;
        snprintf(en->name, sizeof en->name, "%s", kRoots[i].name);
        snprintf(en->full, sizeof en->full, "%s", kRoots[i].path);
        snprintf(en->note, sizeof en->note, "%s \xC2\xB7 %d elemento%s", kRoots[i].note, n, n == 1 ? "" : "s");
    }
}

void browser_open(Browser *b) {
    b->open = 1;
    b->folders_added = 0;
    b->toast[0] = 0;
    browser_show_root(b);
}

static int cmp_entries(const void *pa, const void *pb) {
    const BrowserEntry *a = pa, *b = pb;
    // Folders, then games, then everything else.
    int ga = a->kind == ENTRY_DIR ? 0 : a->kind == ENTRY_GAME ? 1 : 2;
    int gb = b->kind == ENTRY_DIR ? 0 : b->kind == ENTRY_GAME ? 1 : 2;
    if (ga != gb) return ga - gb;
    return strcasecmp(a->name, b->name);
}

int browser_enter(Browser *b, const char *path_in) {
    // path_in may point into b->entries, which is rebuilt below.
    char path[512];
    snprintf(path, sizeof path, "%s", path_in);
    DIR *d = opendir(path);
    if (!d) {
        snprintf(b->error, sizeof b->error, "No se pudo abrir %s (%s)", path,
                 errno == ENOENT ? "no existe" : errno == EACCES || errno == EPERM ? "sin permiso" : strerror(errno));
        return 0;
    }
    b->at_root = 0;
    snprintf(b->path, sizeof b->path, "%s", path);
    b->count = 0;
    b->sel = 0;
    b->scroll = 0;
    b->truncated = 0;
    b->error[0] = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (b->count >= BROWSER_MAX) { b->truncated = 1; break; }
        BrowserEntry *en = &b->entries[b->count];
        memset(en, 0, sizeof *en);
        snprintf(en->name, sizeof en->name, "%s", e->d_name);
        snprintf(en->full, sizeof en->full, "%s%s%s", path, path[strlen(path) - 1] == '/' ? "" : "/", e->d_name);
        if (path_is_dir(en->full)) {
            en->kind = ENTRY_DIR;
            snprintf(en->note, sizeof en->note, "Carpeta");
        } else {
            en->size = path_size(en->full);
            en->kind = (ends_ci(e->d_name, ".iso") || ends_ci(e->d_name, ".cso")) ? ENTRY_GAME : ENTRY_FILE;
            format_size(en->size, en->note, sizeof en->note);
        }
        b->count++;
    }
    closedir(d);
    qsort(b->entries, b->count, sizeof(BrowserEntry), cmp_entries);
    return 1;
}

void browser_up(Browser *b) {
    if (b->at_root) { b->open = 0; return; }
    // At a drive's top folder: back to the drive list.
    for (int i = 0; kRoots[i].path; i++)
        if (strcmp(b->path, kRoots[i].path) == 0) { browser_show_root(b); return; }
    char parent[512];
    snprintf(parent, sizeof parent, "%s", b->path);
    char *slash = strrchr(parent, '/');
    if (!slash) { browser_show_root(b); return; }
    if (slash == parent) slash[1] = 0; else *slash = 0;
    char child[512];
    snprintf(child, sizeof child, "%s", b->path);
    if (!browser_enter(b, parent)) { browser_show_root(b); return; }
    for (int i = 0; i < b->count; i++)   // keep the folder we came from selected
        if (strcmp(b->entries[i].full, child) == 0) { b->sel = i; break; }
}

// ---------------------------------------------------------------- saved folders

const char *library_folders_file(void) {
#ifdef VAULT_HOST
    return "build/host/folders.txt";
#else
    return "/data/vault/folders.txt";
#endif
}

int library_folders_load(char out[][512], int max) {
    FILE *f = fopen(library_folders_file(), "r");
    if (!f) return 0;
    int n = 0;
    char line[512];
    while (n < max && fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        if (l) snprintf(out[n++], 512, "%s", line);
    }
    fclose(f);
    return n;
}

int library_folder_add(const char *path) {
    char list[32][512];
    int n = library_folders_load(list, 32);
    for (int i = 0; i < n; i++) if (strcmp(list[i], path) == 0) return 0;
#ifndef VAULT_HOST
    mkdir("/data/vault", 0777);
#endif
    FILE *f = fopen(library_folders_file(), "a");
    if (!f) return -1;
    fprintf(f, "%s\n", path);
    fclose(f);
    return 1;
}
