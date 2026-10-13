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
    int is_psp;            // PSP_GAME/PARAM.SFO found
    uint64_t file_size;    // bytes on storage
    uint64_t image_size;   // uncompressed bytes
    uint32_t c1, c2;       // palette for glows / fallback art
    Image tile;            // icon scaled to tile size (may be empty)
    Image icon;            // original ICON0 (may be empty)
} Game;

typedef struct {
    Game games[MAX_GAMES];
    int count;
    uint64_t total_bytes;
    int folders_found;
} Library;

// The folders VAULT looks in, NULL terminated. The first entry is created if missing.
extern const char *const kGameDirs[];
extern const char *const kGameDirLabels[];

// tile_w/tile_h: size the icon is pre-scaled to.
void library_scan(Library *lib, int tile_w, int tile_h);
void library_free(Library *lib);

// Loads the big background art (PIC1.PNG, else ICON0) for one game. Caller frees.
Image library_load_cover(const Game *g, int w, int h);

void format_size(uint64_t bytes, char *out, int out_len);
