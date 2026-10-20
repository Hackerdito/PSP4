// browser.h - file explorer state (drives, folders, games). Drawing lives in ui.c.
#pragma once
#include <stdint.h>

#define BROWSER_MAX 1024

typedef enum { ENTRY_DRIVE = 0, ENTRY_DIR, ENTRY_GAME, ENTRY_FILE } EntryKind;

typedef struct {
    char name[200];        // shown
    char full[512];        // absolute path
    char note[48];         // right column: size, "Carpeta", drive description
    EntryKind kind;
    uint64_t size;
} BrowserEntry;

typedef struct {
    int open;
    int at_root;           // listing the drives
    char path[512];
    BrowserEntry entries[BROWSER_MAX];
    int count;
    int sel;
    float scroll;          // first visible row (animated)
    int top;               // first visible row (where scroll is heading)
    int truncated;
    char error[160];       // why a folder couldn't be opened
    char toast[160];       // short feedback line
    int toast_frames;
    int folders_added;     // the library needs a rescan on close
} Browser;

void browser_open(Browser *b);
void browser_show_root(Browser *b);
// Opens a folder; returns 0 and sets b->error if it can't be listed.
int browser_enter(Browser *b, const char *path);
void browser_up(Browser *b);
// Friendly form of a path: "/mnt/usb0/Juegos/x.iso" -> "USB0 / Juegos / x.iso".
void browser_display_path(const char *path, char *out, int out_len);

// Saved library folders (one path per line).
int library_folder_add(const char *path);          // 1 added, 0 already there, -1 error
int library_folders_load(char out[][512], int max);
const char *library_folders_file(void);
