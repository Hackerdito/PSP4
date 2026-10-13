// disc.h - read PSP disc images (.iso / .cso) and their PARAM.SFO / artwork.
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum { DISC_ISO = 0, DISC_CSO = 1 } DiscFormat;

typedef struct Disc Disc;

// Opens an .iso or .cso image. Returns NULL when the file can't be read or isn't a valid image.
Disc *disc_open(const char *path);
void disc_close(Disc *d);

DiscFormat disc_format(const Disc *d);
// Uncompressed size of the image in bytes.
uint64_t disc_size(const Disc *d);

// Reads a whole file from the ISO9660 filesystem inside the image (e.g. "PSP_GAME/PARAM.SFO").
// Returns a malloc'd buffer (caller frees) and stores its length in *len, or NULL if missing.
uint8_t *disc_read_file(Disc *d, const char *path, size_t *len, size_t max_len);

// PARAM.SFO lookup: copies the string value of `key` into out (NUL terminated). Returns 1 if found.
int sfo_get_string(const uint8_t *sfo, size_t len, const char *key, char *out, size_t out_len);
