// platform.h - the few things VAULT needs from the console (or the host test build).
#pragma once
#include <stdint.h>
#include "gfx.h"

enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_TRIANGLE = 1 << 6, BTN_SQUARE = 1 << 7,
    BTN_L1 = 1 << 8, BTN_R1 = 1 << 9, BTN_OPTIONS = 1 << 10,
};

#define SCREEN_W 1920
#define SCREEN_H 1080

// Loads anything that lives in the app package; must run before plat_init() (which escapes the sandbox).
const char *plat_app_dir(void);
int plat_init(void);
uint32_t plat_buttons(void);          // currently held buttons
void plat_present(const Canvas *c);   // shows the canvas and waits for vsync
void plat_idle(void);                 // sleeps ~one frame
void plat_clock(int *hh, int *mm);    // local time
void plat_log(const char *fmt, ...);
// Releases the screen and controller (before the built-in engine takes them over).
void plat_shutdown(void);

// PSP engine: M33 (PPSSPP for PS4, https://github.com/iHaiDeeZ/M33), installed as its own app.
#define ENGINE_TITLE_ID "MRCH00033"
int plat_engine_installed(void);
// Starts the engine with the game's path as its argument. Returns 0, or the system's error code.
int plat_launch_engine(const char *game_path);

#ifdef VAULT_HOST
int plat_host_done(void);
#endif
