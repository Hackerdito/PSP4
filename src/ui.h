// ui.h - VAULT launcher screen.
#pragma once
#include "gfx.h"
#include "library.h"
#include "browser.h"

typedef enum { MODAL_NONE = 0, MODAL_PLAY, MODAL_DETAILS, MODAL_NO_ENGINE, MODAL_LAUNCH_ERROR, MODAL_PS1 } Modal;

typedef struct {
    Library lib;
    int view[MAX_GAMES];     // indices into lib.games after filtering
    int view_count;
    int filter;              // 0 todos, 1 ISO, 2 CSO
    int sel;                 // index into view
    float scroll, scroll_target;
    Modal modal;
    Canvas bg;
    int bg_for;              // game index the background was painted for (-2 = none)
    Image cover;
    int cover_for;
    int idle_frames;
    int scanning;
    int launch_request;      // set by Cross; main.c starts the engine with launch_path
    char launch_path[512];
    Browser browser;         // the Explorar window
    int launch_error;
} App;

void app_init(App *a);
void app_rescan(App *a);
// Applies one frame of input; returns 1 if the screen needs redrawing.
int app_update(App *a, uint32_t pressed, uint32_t held);
void app_draw(App *a, Canvas *c, int hh, int mm);
void app_draw_scanning(Canvas *c, const Canvas *bg);
void app_draw_launching(App *a, Canvas *c);
// Called by main.c with the result of plat_launch_engine.
// Name and system ("PSP" / "PLAYSTATION") of the game in launch_path, for the start-up sequence.
void app_launch_info(App *a, char *sys, int sys_len, char *title, int title_len);
void app_launch_failed(App *a, int code);
