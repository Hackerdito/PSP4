// platform.h - the few things VAULT needs from the console (or the host test build).
#pragma once
#include <stdint.h>
#include "gfx.h"

enum {
    BTN_UP = 1 << 0, BTN_DOWN = 1 << 1, BTN_LEFT = 1 << 2, BTN_RIGHT = 1 << 3,
    BTN_CROSS = 1 << 4, BTN_CIRCLE = 1 << 5, BTN_TRIANGLE = 1 << 6, BTN_SQUARE = 1 << 7,
    BTN_L1 = 1 << 8, BTN_R1 = 1 << 9, BTN_OPTIONS = 1 << 10,
    BTN_L2 = 1 << 11, BTN_R2 = 1 << 12, BTN_L3 = 1 << 13, BTN_R3 = 1 << 14, BTN_TOUCH = 1 << 15,
};

// Full controller state (games): digital buttons without the stick-as-d-pad mapping, sticks 0..255.
typedef struct { uint32_t buttons; uint8_t lx, ly, rx, ry; } PadState;

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

// Games draw straight into the screen: the buffer for the next frame (SCREEN_W x SCREEN_H XRGB),
// then plat_frame_end() shows it at the next vsync without waiting (three buffers in turn).
uint32_t *plat_frame_begin(void);
// 0 while two frames are still waiting for the screen: a game should skip drawing this one
// rather than wait (the emulation keeps its speed; the screen just repeats a frame).
int plat_frame_ready(void);
// Where a game's frame time goes (the PS1 player logs these).
typedef struct { uint64_t flip_wait_us, audio_wait_us, underruns, audio_dropped; } PlatStats;
extern PlatStats plat_stats;
void plat_frame_end(void);
int plat_pad(PadState *out);
// 48 kHz stereo 16-bit. plat_audio_play blocks while the device's queue is full, so a game loop
// that feeds it each frame runs at the audio's pace.
int plat_audio_open(void);
void plat_audio_play(const int16_t *stereo, int frames);
void plat_audio_close(void);
uint64_t plat_time_us(void);
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
