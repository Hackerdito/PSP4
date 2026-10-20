// disc.h - read PSP disc images (.iso / .cso) and their PARAM.SFO / artwork.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum { DISC_ISO = 0, DISC_CSO = 1, DISC_PBP = 2, DISC_CHD = 3, DISC_CUE = 4, DISC_BIN = 5 } DiscFormat;
#define DISC_FORMATS 6

// Game file formats by extension: .iso .cso .pbp (EBOOT.PBP) .chd .cue .bin. Returns -1 for anything else.
int disc_format_from_name(const char *name);
const char *disc_format_name(DiscFormat f);   // "ISO", "CSO", "PBP", "CHD", "CUE", "BIN"

// EBOOT.PBP: entries 0 PARAM.SFO, 1 ICON0.PNG, 2 ICON1.PMF, 3 PIC0.PNG, 4 PIC1.PNG, 5 SND0.AT3,
// 6 DATA.PSP, 7 DATA.PSAR. Returns a malloc'd copy of one entry (NULL if empty or too big).
enum { PBP_SFO = 0, PBP_ICON0 = 1, PBP_PIC1 = 4, PBP_PSAR = 7 };
uint8_t *pbp_read_entry(const char *path, int entry, size_t *len, size_t max_len);
// A PlayStation (PS1) classic packed as EBOOT.PBP (PSN "PSone Classics"), not a PSP game.
int pbp_is_ps1(const char *path);

// A .chd holding a CD (PS1) rather than a DVD/UMD (PSP): read from its metadata, no decompression.
int chd_is_cd(const char *path);

typedef struct Disc Disc;

// Opens an .iso, .cso, .bin (raw 2352-byte sectors) or .cue (its first data track). Returns NULL when the file can't be read or isn't a valid image.
Disc *disc_open(const char *path);
void disc_close(Disc *d);

DiscFormat disc_format(const Disc *d);
// Bytes of the image file itself (for a .cue, its .bin). 0 when not known.
uint64_t disc_file_bytes(const Disc *d);
// Uncompressed size of the image in bytes.
uint64_t disc_size(const Disc *d);

// Reads a whole file from the ISO9660 filesystem inside the image (e.g. "PSP_GAME/PARAM.SFO").
// Returns a malloc'd buffer (caller frees) and stores its length in *len, or NULL if missing.
uint8_t *disc_read_file(Disc *d, const char *path, size_t *len, size_t max_len);

// PARAM.SFO lookup: copies the string value of `key` into out (NUL terminated). Returns 1 if found.
int sfo_get_string(const uint8_t *sfo, size_t len, const char *key, char *out, size_t out_len);

// PS1 discs: the boot executable named in SYSTEM.CNF, as a disc ID ("SLUS00594"). 0 if not a PS1 disc.
int ps1_disc_id(Disc *d, char *out, size_t out_len);
