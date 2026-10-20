// library.h - scans the VAULT game folders for PSP .iso / .cso images.
#pragma once
#include <stdint.h>
#include "gfx.h"
#include "disc.h"

#define MAX_GAMES 512

typedef struct {
    char path[512];
    char file[256];
    char title[160];
    char disc_id[16];      // e.g. ULUS10041
    char source[16];       // "USB0", "Interno"
    DiscFormat fmt;
    int is_psp;            // PARAM.SFO found
    int is_ps1;            // a PS1 classic in an EBOOT.PBP (needs the PS1 engine; not playable yet)
    uint64_t file_size;    // bytes on storage
    uint64_t image_size;   // uncompressed bytes
    uint32_t c1, c2;       // palette for glows / fallback art
    Image tile;            // icon scaled to tile size (may be empty)
    Image icon;            // original ICON0 (may be empty)
} Game;

// What the scan saw in one folder, shown on screen when the library is empty.
typedef struct {
    char path[256];        // folder as configured
    char used[256];        // folder actually opened (case-insensitive match), "" if none
    int exists;
    int err;               // errno from opendir when it doesn't
    int files, games, subdirs;
    char other[96];        // e.g. ".zip x2, .rar x1"
    char sample[96];       // first non-game file name, to show
} DirReport;

#define MAX_REPORTS 12

typedef struct {
    Game games[MAX_GAMES];
    int count;
    uint64_t total_bytes;
    int folders_found;
    DirReport reports[MAX_REPORTS];
    int report_count;
    int data_readable;     // /data could be listed (sandbox escaped)
} Library;

typedef struct {
    const char *path;
    const char *label;     // shown on the game ("Interno", "USB1")
    int max_depth;         // subfolder levels searched
    int optional;          // USB ports: only reported when a drive is there
} GameDir;

// The folders VAULT looks in, ended by a NULL path. Non-optional ones are created if missing.
extern const GameDir kGameDirs[];

// tile_w/tile_h: size the icon is pre-scaled to.
void library_scan(Library *lib, int tile_w, int tile_h);
void library_free(Library *lib);

// Loads the big background art (PIC1.PNG, else ICON0) for one game. Caller frees.
Image library_load_cover(const Game *g, int w, int h);

void format_size(uint64_t bytes, char *out, int out_len);

// File-system helpers that avoid stat(): the console's libkernel fills a FreeBSD 9 struct stat,
// whose st_size sits 8 bytes before where the C library's header expects it.
int path_exists(const char *path);
int path_is_dir(const char *path);
uint64_t path_size(const char *path);
