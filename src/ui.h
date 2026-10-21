// ui.h - VAULT launcher screen.
#pragma once
#include "gfx.h"
#include "library.h"
#include "browser.h"
#include "netpc.h"
#include <stdint.h>

typedef enum { MODAL_NONE = 0, MODAL_PLAY, MODAL_DETAILS, MODAL_NO_ENGINE, MODAL_LAUNCH_ERROR, MODAL_PS1 } Modal;

// "Mi PC": browsing a computer's shared folder and copying games (netpc.c does the network).
typedef enum { PCV_SETUP, PCV_SHARES, PCV_DIR, PCV_DEST, PCV_DOWNLOAD, PCV_RESULT } PcScreen;

typedef struct {
    int open;
    PcScreen screen;
    PcConfig cfg;
    PcJob job;                   // owned by the worker thread while job.state == JOB_RUNNING
    int busy;                    // a job whose result this screen is waiting for
    int ignore_result;           // left the wait with Circle: drop the result when it comes
    PcScreen after;              // screen to show when a listing arrives
    char msg[200];               // error / info line
    PcEntry entries[PC_MAX_ENTRIES];
    int count;
    PcEntry shares[64];
    int share_count;
    char dir[512];               // folder inside the share
    int sel, top;
    float scroll;
    // Setup form: 0 host, 1 user, 2 password, 3 search, 4 connect, then found computers
    int field;
    char found[PC_MAX_FOUND][64];
    int found_count;
    int searched;
    // On-screen keyboard
    int kb_open, kb_field, kb_row, kb_col, kb_shift;
    char kb_buf[64];
    // Destination for a download
    char dest_name[9][40];
    char dest_path[9][160];
    int dest_count, dest_sel;
    char pick[200];              // the game being copied (name)
    char pick_remote[512];
    int downloaded;              // the library needs a rescan
    char last_dest[512];
    uint64_t speed_done, speed_t;
    float speed;                 // bytes per second
    int frame;
} PcView;

// "Subir desde el celular" (R2): el servidor de upload.c y su pantalla.
typedef struct {
    int open;
    int frame;
    int added_at_open;           // upload_status()->games_added al abrir: si cambia, se reescanea
    uint64_t last_sig;
    char qr_for[64];             // URL con la que se armó el QR
    int qr_ok;
    uint8_t qr[256];             // qrcodegen_BUFFER_LEN_FOR_VERSION(6) = 211
} UploadView;

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
    PcView pc;               // the "Mi PC" window
    UploadView upload;       // "Subir desde el celular"
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
