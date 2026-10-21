// platform_host.c - desktop test harness: scripted input, screenshots to PNG.
// Build with `make host` and run from the repo root; screenshots land in build/host/.
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

static int g_frame = 0;
static const Canvas *g_last = NULL;

typedef struct { int frame; uint32_t buttons; const char *shot; } Step;
static const Step kScript[] = {
    { 30, 0, "01_inicio" },
    { 32, BTN_RIGHT, NULL },
    { 60, 0, "02_siguiente" },
    { 62, BTN_TRIANGLE, NULL },
    { 66, 0, "03_detalles" },
    { 68, BTN_CIRCLE, NULL },
    { 70, BTN_CROSS, NULL },
    { 74, 0, "04_jugar" },
    { 76, BTN_CIRCLE, NULL },
    { 78, BTN_R1, NULL },
    { 80, BTN_R1, NULL },
    { 110, 0, "05_filtro_cso" },
    { 112, BTN_R1, NULL },
    { 114, BTN_RIGHT, NULL }, { 116, BTN_RIGHT, NULL }, { 118, BTN_RIGHT, NULL },
    { 120, BTN_RIGHT, NULL }, { 122, BTN_RIGHT, NULL }, { 124, BTN_RIGHT, NULL },
    { 170, 0, "06_scroll" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=explorar: opens the Explorar window, enters a USB, adds the folder, closes.
static const Step kBrowserScript[] = {
    { 30, 0, "07_inicio" },
    { 32, BTN_SQUARE, NULL },
    { 40, 0, "08_explorar_unidades" },
    { 41, BTN_DOWN, NULL },
    { 42, BTN_CROSS, NULL },
    { 50, 0, "09_explorar_usb0" },
    { 52, BTN_DOWN, NULL },
    { 54, BTN_CROSS, NULL },
    { 70, 0, "10_explorar_carpeta" },
    { 72, BTN_TRIANGLE, NULL },
    { 76, 0, "11_carpeta_agregada" },
    { 78, BTN_CIRCLE, NULL }, { 80, BTN_CIRCLE, NULL }, { 82, BTN_CIRCLE, NULL },
    { 110, 0, "12_biblioteca" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=eboot: the EBOOT section and a PSP EBOOT's details, then the PS1 section.
static const Step kEbootScript[] = {
    { 30, BTN_R1, NULL },
    { 70, 0, "13_filtro_eboot" },
    { 72, BTN_TRIANGLE, NULL },
    { 76, 0, "14_detalles_eboot" },
    { 78, BTN_CIRCLE, NULL },
    { 80, BTN_RIGHT, NULL },
    { 100, BTN_CROSS, NULL },
    { 104, 0, "15_ps1_aviso" },
    { 106, BTN_CIRCLE, NULL },
    { 108, BTN_R1, NULL },
    { 140, 0, "16_seccion_ps1" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=scroll: a long folder in Explorar, moving down past the last visible row.
static const Step kScrollScript[] = {
    { 30, BTN_SQUARE, NULL },
    { 36, BTN_DOWN, NULL },
    { 40, BTN_CROSS, NULL },
    { 46, BTN_CROSS, NULL },
    { 50, BTN_DOWN, NULL },
    { 54, BTN_DOWN, NULL },
    { 58, BTN_DOWN, NULL },
    { 62, BTN_DOWN, NULL },
    { 66, BTN_DOWN, NULL },
    { 70, BTN_DOWN, NULL },
    { 74, BTN_DOWN, NULL },
    { 78, BTN_DOWN, NULL },
    { 82, BTN_DOWN, NULL },
    { 86, BTN_DOWN, NULL },
    { 90, BTN_DOWN, NULL },
    { 94, BTN_DOWN, NULL },
    { 98, BTN_DOWN, NULL },
    { 102, BTN_DOWN, NULL },
    { 136, 0, "17_scroll_abajo" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=secciones: each library section (ISO / CSO, EBOOT y Homebrew, PS1).
static const Step kSectionsScript[] = {
    { 40, 0, "18_seccion_psp" },
    { 42, BTN_R1, NULL },
    { 80, 0, "19_seccion_eboot" },
    { 82, BTN_R1, NULL },
    { 86, BTN_RIGHT, NULL },
    { 130, 0, "20_seccion_ps1" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=ps1 (with VAULT_PS1=<game>): frames from the PS1 player, then the pause menu.
static const Step kPs1Script[] = {
    { 120, 0, "21_ps1_juego" },
    { 200, BTN_L3 | BTN_R3, NULL },
    { 206, 0, "23_ps1_menu" },
    { 208, BTN_DOWN, NULL }, { 210, BTN_DOWN, NULL }, { 212, BTN_DOWN, NULL },   // -> Tamano
    { 214, BTN_RIGHT, NULL },                                                    // pantalla completa
    { 216, BTN_DOWN, NULL }, { 218, BTN_RIGHT, NULL },                           // filtro suave
    { 222, 0, "24_ps1_menu_ajustes" },
    { 224, BTN_CIRCLE, NULL },
    { 260, 0, "25_ps1_completa_suave" },
    { 262, BTN_L3 | BTN_R3, NULL },
    { 266, BTN_DOWN, NULL }, { 268, BTN_DOWN, NULL }, { 270, BTN_DOWN, NULL }, { 272, BTN_LEFT, NULL },   // tamano: 4:3
    { 274, BTN_DOWN, NULL }, { 276, BTN_RIGHT, NULL },                          // filtro: TV
    { 278, BTN_DOWN, NULL }, { 280, BTN_DOWN, NULL }, { 282, BTN_RIGHT, NULL }, // mostrar FPS
    { 284, BTN_CIRCLE, NULL },
    { 330, 0, "26_ps1_tv_fps" },
    { 332, BTN_L3 | BTN_R3, NULL },
    { 336, BTN_UP, NULL }, { 338, BTN_UP, NULL }, { 340, BTN_UP, NULL }, { 342, BTN_UP, NULL },  // -> Ver controles
    { 346, BTN_CROSS, NULL },
    { 352, 0, "27_ps1_controles" },
    { 354, BTN_CIRCLE, NULL },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=intro: the start-up sequence when a game is launched.
static const Step kIntroScript[] = {
    { 30, BTN_CROSS, NULL },
    { 46, 0, "25_intro_a" },
    { 76, 0, "26_intro_b" },
    { 110, 0, "27_intro_c" },
    { 0, 0, NULL },
};

// VAULT_SCRIPT=mipc (a saved build/host/pc.txt pointing at a test SMB share): Mi PC from L2,
// its shared folders, a folder, a PS1 game copied to the PS4, then the settings and keyboard.
// "Subir desde el celular": tests/upload_shots.py sube un juego por la red mientras corre.
static const Step kUploadScript[] = {
    { 20, BTN_R2, NULL },
    { 60, 0, "34_celular_esperando" },
    { 1300, 0, "35_celular_recibiendo" },
    { 2300, 0, "36_celular_listo" },
    { 2310, BTN_CIRCLE, NULL },
    { 2400, 0, "37_celular_biblioteca" },
    { 0, 0, NULL },
};

static const Step kPcScript[] = {
    { 20, BTN_L2, NULL },
    { 120, 0, "28_mipc_carpetas" },
    { 122, BTN_CROSS, NULL },
    { 220, 0, "29_mipc_raiz" },
    { 222, BTN_DOWN, NULL },
    { 226, BTN_CROSS, NULL },
    { 320, 0, "30_mipc_ps1" },
    { 322, BTN_DOWN, NULL },
    { 326, BTN_CROSS, NULL },
    { 340, 0, "31_mipc_destino" },
    { 342, BTN_CROSS, NULL },
    { 345, 0, "32_mipc_descargando" },
    { 460, 0, "33_mipc_listo" },
    { 462, BTN_CROSS, NULL },
    { 470, BTN_TRIANGLE, NULL },
    { 480, 0, "34_mipc_ajustes" },
    { 482, BTN_CROSS, NULL },
    { 486, BTN_DOWN, NULL }, { 488, BTN_RIGHT, NULL },
    { 494, 0, "35_mipc_teclado" },
    { 0, 0, NULL },
};

const char *plat_app_dir(void) {
    const char *d = getenv("VAULT_APP");
    return d ? d : ".";
}

int plat_init(void) { return 1; }

void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("[VAULT] ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static void shot(const char *name) {
    if (!g_last) return;
    const char *out = getenv("VAULT_SHOTS");
    char path[512];
    snprintf(path, sizeof path, "%s/%s.png", out ? out : "build/host", name);
    int n = g_last->w * g_last->h;
    unsigned char *rgb = malloc((size_t)n * 3);
    for (int i = 0; i < n; i++) {
        uint32_t p = g_last->px[i];
        rgb[i * 3] = (p >> 16) & 255; rgb[i * 3 + 1] = (p >> 8) & 255; rgb[i * 3 + 2] = p & 255;
    }
    stbi_write_png(path, g_last->w, g_last->h, 3, rgb, g_last->w * 3);
    free(rgb);
    printf("[VAULT] screenshot %s\n", path);
}

uint32_t plat_buttons(void) {
    g_frame++;
    uint32_t b = 0;
    const char *sc = getenv("VAULT_SCRIPT");
    const Step *script = (sc && strcmp(sc, "explorar") == 0) ? kBrowserScript
                       : (sc && strcmp(sc, "eboot") == 0) ? kEbootScript
                       : (sc && strcmp(sc, "scroll") == 0) ? kScrollScript
                       : (sc && strcmp(sc, "secciones") == 0) ? kSectionsScript
                       : (sc && strcmp(sc, "ps1") == 0) ? kPs1Script
                       : (sc && strcmp(sc, "intro") == 0) ? kIntroScript
                       : (sc && strcmp(sc, "mipc") == 0) ? kPcScript
                       : (sc && strcmp(sc, "celular") == 0) ? kUploadScript : kScript;
    for (const Step *s = script; s->frame; s++) {
        if (s->frame != g_frame) continue;
        b |= s->buttons;
        if (s->shot) shot(s->shot);
    }
    return b;
}

// Keeps its own copy: callers may free their canvas right after (the intro does).
void plat_present(const Canvas *c) {
    static Canvas copy;
    if (!copy.px) copy = canvas_new(SCREEN_W, SCREEN_H);
    memcpy(copy.px, c->px, (size_t)c->w * c->h * 4);
    g_last = &copy;
}
void plat_idle(void) { struct timespec ts = { 0, 4000000 }; nanosleep(&ts, NULL); }

void plat_clock(int *hh, int *mm) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    *hh = tm.tm_hour;
    *mm = tm.tm_min;
}

int plat_host_done(void) {
    const char *once = getenv("VAULT_ONE_SHOT");
    if (once && g_frame >= 30) return 1;
    int last = 170;
    const char *sc = getenv("VAULT_SCRIPT");
    const Step *script = (sc && strcmp(sc, "ps1") == 0) ? kPs1Script : (sc && strcmp(sc, "mipc") == 0) ? kPcScript
                       : (sc && strcmp(sc, "celular") == 0) ? kUploadScript : NULL;
    if (script) for (const Step *st = script; st->frame; st++) if (st->frame > last) last = st->frame;
    return g_frame > last + 10;
}

int plat_engine_installed(void) { return getenv("VAULT_NO_ENGINE") == NULL; }
int plat_launch_engine(const char *game_path) {
    printf("[VAULT] would launch %s with %s\n", ENGINE_TITLE_ID, game_path);
    shot("04_abriendo");
    const char *fail = getenv("VAULT_LAUNCH_FAIL");
    return fail ? (int)strtoul(fail, NULL, 16) : 0;
}

void plat_shutdown(void) {}

// ---------------------------------------------------------------- game-side API (host)
// Frames go into one canvas (screenshots via the script); audio is counted, not played.
static Canvas g_game_canvas;

uint32_t *plat_frame_begin(void) {
    if (!g_game_canvas.px) g_game_canvas = canvas_new(SCREEN_W, SCREEN_H);
    return g_game_canvas.px;
}

void plat_frame_end(void) { g_last = &g_game_canvas; }
PlatStats plat_stats;
void plat_rumble(uint8_t big, uint8_t small) { (void)big; (void)small; }
int plat_frame_ready(void) { return 1; }

int plat_pad(PadState *out) {
    memset(out, 0, sizeof *out);
    out->lx = out->ly = out->rx = out->ry = 128;
    out->buttons = plat_buttons();
    return 1;
}

static uint64_t g_audio_frames;
int plat_audio_open(void) { g_audio_frames = 0; return 1; }
void plat_audio_play(const int16_t *stereo, int frames) { (void)stereo; g_audio_frames += (uint64_t)frames; }
void plat_audio_close(void) { plat_log("audio: %llu frames played", (unsigned long long)g_audio_frames); }

uint64_t plat_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}
