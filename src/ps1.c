// ps1.c - SAMPSP4's PlayStation 1 player: a small libretro front end around PCSX-ReARMed
// (https://github.com/libretro/pcsx_rearmed, GPLv2), linked into the same eboot.
//
// Video: the core's frame (256..640 x 240/480, RGB565 or XRGB8888) scaled straight into the
// screen buffer at 4:3 (1440x1080, centered). Audio: 44.1 kHz from the core, resampled to the
// console's 48 kHz; feeding it paces the game. Menu: L3 + R3 (or touch pad + OPTIONS).
// Memory cards: <root>/saves/<game>.srm (RetroArch's format). Save states: <root>/states/.
// BIOS: <root>/bios/scph*.bin if the player has one, else the core's built-in HLE BIOS.
#include "ps1.h"

#include "disc.h"
#include "gfx.h"
#include "library.h"
#include "platform.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "libretro/libretro.h"

#ifdef VAULT_HOST
#define PS1_ROOT "build/host/ps1"
#else
#define PS1_ROOT "/data/vault/ps1"
#endif

// The core, linked in with its symbols renamed to ps1core_* (engine/build-ps1.sh).
#define CORE(fn) ps1core_##fn
void CORE(retro_set_environment)(retro_environment_t);
void CORE(retro_set_video_refresh)(retro_video_refresh_t);
void CORE(retro_set_audio_sample)(retro_audio_sample_t);
void CORE(retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
void CORE(retro_set_input_poll)(retro_input_poll_t);
void CORE(retro_set_input_state)(retro_input_state_t);
void CORE(retro_init)(void);
void CORE(retro_deinit)(void);
void CORE(retro_get_system_av_info)(struct retro_system_av_info *);
void CORE(retro_set_controller_port_device)(unsigned, unsigned);
void CORE(retro_reset)(void);
void CORE(retro_run)(void);
size_t CORE(retro_serialize_size)(void);
bool CORE(retro_serialize)(void *, size_t);
bool CORE(retro_unserialize)(const void *, size_t);
bool CORE(retro_load_game)(const struct retro_game_info *);
void CORE(retro_unload_game)(void);
void *CORE(retro_get_memory_data)(unsigned);
size_t CORE(retro_get_memory_size)(unsigned);

#define OUT_W 1440                     // 4:3 inside 1920x1080
#define OUT_X ((SCREEN_W - OUT_W) / 2)

static struct {
    enum retro_pixel_format fmt;
    const void *frame;                 // last frame from the core (stays valid in PCSX)
    unsigned fw, fh;
    size_t pitch;
    unsigned last_w, last_h;
    int borders_left;                  // buffers whose side bars still need clearing
    PadState pad;
    double sample_rate;
    double phase;                      // resampler position between in[0] and in[1]
    int16_t hist[2];                   // last input frame of the previous batch
    char sys_dir[256], save_dir[256];
    FILE *log;
    uint32_t *target;                  // where video_refresh draws (screen or the menu snapshot)
    uint64_t dropped;                  // frames not shown because the screen was busy
    uint64_t blit_us;
} S;

static void slog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    plat_log("ps1: %s", buf);
    if (S.log) { fprintf(S.log, "%s\n", buf); fflush(S.log); }
}

static void mkdirs(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    mkdir(tmp, 0777);
}

// ---------------------------------------------------------------- detection

int ps1_is_game(const char *path) {
    int fmt = disc_format_from_name(path);
    if (fmt == DISC_CUE || fmt == DISC_BIN) return 1;
    if (fmt == DISC_PBP) return pbp_is_ps1(path);
    if (fmt == DISC_CHD) return chd_is_cd(path);
    if (fmt == DISC_ISO) {
        Disc *d = disc_open(path);
        if (!d) return 0;
        size_t len;
        uint8_t *sfo = disc_read_file(d, "PSP_GAME/PARAM.SFO", &len, 64 * 1024);
        char id[16];
        int ps1 = !sfo && ps1_disc_id(d, id, sizeof id);
        free(sfo);
        disc_close(d);
        return ps1;
    }
    return 0;
}

// ---------------------------------------------------------------- libretro callbacks

// Settings we choose; everything else stays at the core's default.
static const char *option(const char *key) {
    static const struct { const char *k, *v; } opts[] = {
        { "pcsx_rearmed_drc", "enabled" },              // lightrec dynarec (x86-64 JIT)
        { "pcsx_rearmed_bios", "auto" },                // a real BIOS when there is one
        { "pcsx_rearmed_show_bios_bootlogo", "disabled" },
        { "pcsx_rearmed_memcard1", "libretro" },        // card 1 = RETRO_MEMORY_SAVE_RAM (our .srm)
        { "pcsx_rearmed_memcard2", "none" },
        { "pcsx_rearmed_frameskip_type", "disabled" },
        { "pcsx_rearmed_spu_interpolation", "simple" },
        { "pcsx_rearmed_dithering", "enabled" },
    };
#ifdef VAULT_HOST
    if (strcmp(key, "pcsx_rearmed_drc") == 0 && getenv("PS1_INTERPRETER")) return "disabled";
#endif
    for (size_t i = 0; i < sizeof opts / sizeof opts[0]; i++)
        if (strcmp(opts[i].k, key) == 0) return opts[i].v;
    return NULL;
}

static void RETRO_CALLCONV core_log(enum retro_log_level level, const char *fmt, ...) {
    if (level < RETRO_LOG_INFO) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    slog("[core] %s", buf);
}

static bool env(unsigned cmd, void *data) {
    switch (cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format f = *(const enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_XRGB8888) return false;
        S.fmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = S.sys_dir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = S.save_dir; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = option(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;   // legacy variables
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_SPANISH; return true;
    default: return false;
    }
}

// Scales the core's frame into `dst` (stride SCREEN_W) at 4:3, nearest neighbour.
static void blit_frame(uint32_t *dst) {
    if (!S.frame || !S.fw || !S.fh) return;
    static int xmap[OUT_W];
    static unsigned map_w;
    if (map_w != S.fw) {
        for (int x = 0; x < OUT_W; x++) xmap[x] = (int)((uint64_t)x * S.fw / OUT_W);
        map_w = S.fw;
    }
    static uint32_t row[1024];
    int prev_sy = -1;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *out = dst + (size_t)y * SCREEN_W + OUT_X;
        int sy = (int)((uint64_t)y * S.fh / SCREEN_H);
        if (sy == prev_sy) { memcpy(out, out - SCREEN_W, OUT_W * 4); continue; }
        prev_sy = sy;
        const uint8_t *src = (const uint8_t *)S.frame + (size_t)sy * S.pitch;
        unsigned w = S.fw > 1024 ? 1024 : S.fw;
        if (S.fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *p = (const uint32_t *)src;
            for (unsigned x = 0; x < w; x++) row[x] = p[x] | 0xFF000000u;
        } else {
            const uint16_t *p = (const uint16_t *)src;
            for (unsigned x = 0; x < w; x++) {
                uint32_t c = p[x];
                uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
                row[x] = 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
            }
        }
        for (int x = 0; x < OUT_W; x++) out[x] = row[xmap[x]];
    }
}

static void clear_bars(uint32_t *dst) {
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *line = dst + (size_t)y * SCREEN_W;
        for (int x = 0; x < OUT_X; x++) line[x] = 0xFF000000u;
        for (int x = OUT_X + OUT_W; x < SCREEN_W; x++) line[x] = 0xFF000000u;
    }
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;   // duplicate frame: keep showing the last one
    S.frame = data; S.fw = w; S.fh = h; S.pitch = pitch;
    if (w != S.last_w || h != S.last_h) {
        slog("video %ux%u", w, h);
        S.last_w = w; S.last_h = h;
        S.borders_left = 3;
    }
    if (!plat_frame_ready()) { S.dropped++; return; }   // screen still busy: skip, don't wait
    uint32_t *fb = plat_frame_begin();
    if (S.borders_left > 0) { clear_bars(fb); S.borders_left--; }
    uint64_t t = plat_time_us();
    blit_frame(fb);
    S.blit_us += plat_time_us() - t;
    plat_frame_end();
}

// 44.1 kHz (or whatever the core says) -> 48 kHz, linear.
static size_t audio_batch(const int16_t *in, size_t frames) {
    static int16_t out[4096 * 2];
    double step = S.sample_rate / 48000.0;
    size_t n = 0;
    while (frames) {
        // Position `phase` runs over [hist, in[0], in[1], ...].
        while (S.phase < (double)frames && n < 4096) {
            int i = (int)S.phase;
            double f = S.phase - i;
            const int16_t *a = i == 0 ? S.hist : in + (i - 1) * 2;
            const int16_t *b = in + i * 2;
            out[n * 2] = (int16_t)(a[0] + (b[0] - a[0]) * f);
            out[n * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * f);
            n++;
            S.phase += step;
        }
        S.hist[0] = in[(frames - 1) * 2];
        S.hist[1] = in[(frames - 1) * 2 + 1];
        S.phase -= (double)frames;
        if (S.phase < 0) S.phase = 0;
        break;
    }
    plat_audio_play(out, (int)n);
    return frames;
}

static void audio_sample(int16_t l, int16_t r) {
    int16_t s[2] = { l, r };
    audio_batch(s, 1);
}

static void input_poll(void) { plat_pad(&S.pad); }

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port != 0) return 0;
    uint32_t b = S.pad.buttons;
    // Left stick doubles as the d-pad for digital-only games.
    if (S.pad.lx < 48) b |= BTN_LEFT;
    if (S.pad.lx > 208) b |= BTN_RIGHT;
    if (S.pad.ly < 48) b |= BTN_UP;
    if (S.pad.ly > 208) b |= BTN_DOWN;
    static const uint32_t map[16] = {
        [RETRO_DEVICE_ID_JOYPAD_B] = BTN_CROSS, [RETRO_DEVICE_ID_JOYPAD_Y] = BTN_SQUARE,
        [RETRO_DEVICE_ID_JOYPAD_SELECT] = BTN_TOUCH, [RETRO_DEVICE_ID_JOYPAD_START] = BTN_OPTIONS,
        [RETRO_DEVICE_ID_JOYPAD_UP] = BTN_UP, [RETRO_DEVICE_ID_JOYPAD_DOWN] = BTN_DOWN,
        [RETRO_DEVICE_ID_JOYPAD_LEFT] = BTN_LEFT, [RETRO_DEVICE_ID_JOYPAD_RIGHT] = BTN_RIGHT,
        [RETRO_DEVICE_ID_JOYPAD_A] = BTN_CIRCLE, [RETRO_DEVICE_ID_JOYPAD_X] = BTN_TRIANGLE,
        [RETRO_DEVICE_ID_JOYPAD_L] = BTN_L1, [RETRO_DEVICE_ID_JOYPAD_R] = BTN_R1,
        [RETRO_DEVICE_ID_JOYPAD_L2] = BTN_L2, [RETRO_DEVICE_ID_JOYPAD_R2] = BTN_R2,
        [RETRO_DEVICE_ID_JOYPAD_L3] = BTN_L3, [RETRO_DEVICE_ID_JOYPAD_R3] = BTN_R3,
    };
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
            int16_t m = 0;
            for (int i = 0; i < 16; i++) if (b & map[i]) m |= (int16_t)(1 << i);
            return m;
        }
        return id < 16 && (b & map[id]) ? 1 : 0;
    }
    if (device == RETRO_DEVICE_ANALOG && id < 2) {
        uint8_t v = index == RETRO_DEVICE_INDEX_ANALOG_LEFT ? (id ? S.pad.ly : S.pad.lx)
                                                            : (id ? S.pad.ry : S.pad.rx);
        int s = ((int)v - 128) * 256;
        return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    return 0;
}

// ---------------------------------------------------------------- memory card / states

static char g_base[200];   // game file name without extension

static void game_paths(const char *dir, const char *ext, char *out, size_t out_len) {
    snprintf(out, out_len, "%s/%s%s", dir, g_base, ext);
}

static uint8_t *g_card_shadow;

static void card_load(void) {
    uint8_t *mem = CORE(retro_get_memory_data)(RETRO_MEMORY_SAVE_RAM);
    size_t size = CORE(retro_get_memory_size)(RETRO_MEMORY_SAVE_RAM);
    if (!mem || !size) return;
    char path[512];
    game_paths(S.save_dir, ".srm", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (f) {
        size_t n = fread(mem, 1, size, f);
        fclose(f);
        slog("memory card %s (%zu bytes)", path, n);
    }
    g_card_shadow = malloc(size);
    memcpy(g_card_shadow, mem, size);
}

static void card_save(int force) {
    uint8_t *mem = CORE(retro_get_memory_data)(RETRO_MEMORY_SAVE_RAM);
    size_t size = CORE(retro_get_memory_size)(RETRO_MEMORY_SAVE_RAM);
    if (!mem || !size || !g_card_shadow) return;
    if (!force && memcmp(mem, g_card_shadow, size) == 0) return;
    char path[512], tmp[520];
    game_paths(S.save_dir, ".srm", path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return;
    int ok = fwrite(mem, 1, size, f) == size;
    ok &= fclose(f) == 0;
    if (ok) { rename(tmp, path); memcpy(g_card_shadow, mem, size); slog("memory card saved"); }
}

static int state_save(void) {
    size_t size = CORE(retro_serialize_size)();
    if (!size) return 0;
    void *buf = malloc(size);
    int ok = CORE(retro_serialize)(buf, size);
    if (ok) {
        char dir[300], path[512];
        snprintf(dir, sizeof dir, PS1_ROOT "/states");
        mkdirs(dir);
        game_paths(dir, ".state", path, sizeof path);
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(buf, 1, size, f) == size;
        if (f) fclose(f);
    }
    free(buf);
    return ok;
}

static int state_load(void) {
    char path[512];
    game_paths(PS1_ROOT "/states", ".state", path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = malloc(size > 0 ? (size_t)size : 1);
    int ok = size > 0 && fread(buf, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    if (ok) ok = CORE(retro_unserialize)(buf, (size_t)size);
    free(buf);
    return ok;
}

// ---------------------------------------------------------------- pause menu

#define C_BG     0x07080C
#define C_PANEL  0x11131A
#define C_TEXT   0xF2F4F8
#define C_MUTED  0xA3A9B8
#define C_ACCENT 0x3DF5C8
#define C_BRAND  0x3F7BFF

enum { M_RESUME, M_SAVE, M_LOAD, M_RESET, M_QUIT, M_COUNT };

// Returns 1 to keep playing, 0 to quit.
static int pause_menu(const char *title) {
    static const char *items[M_COUNT] = { "Continuar", "Guardar estado", "Cargar estado", "Reiniciar juego", "Salir a SAMPSP4" };
    Canvas snap = canvas_new(SCREEN_W, SCREEN_H);
    memset(snap.px, 0, (size_t)SCREEN_W * SCREEN_H * 4);
    blit_frame(snap.px);
    // Dim the game behind the menu once.
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) {
        uint32_t p = snap.px[i];
        snap.px[i] = ((p >> 2) & 0x3F3F3F) + ((p >> 3) & 0x1F1F1F);
    }
    Canvas c = canvas_new(SCREEN_W, SCREEN_H);
    char note[96] = "";
    char path[512];
    game_paths(PS1_ROOT "/states", ".state", path, sizeof path);
    int has_state = path_exists(path);
    int sel = 0, result = -1;
    uint32_t prev = ~0u;   // ignore buttons still held from the shortcut
    PadState p;
    while (result < 0) {
        plat_pad(&p);
        uint32_t held = p.buttons;
        if (p.ly < 48) held |= BTN_UP;
        if (p.ly > 208) held |= BTN_DOWN;
        uint32_t pressed = held & ~prev;
        prev = held;
        if ((pressed & BTN_DOWN) && sel < M_COUNT - 1) sel++;
        if ((pressed & BTN_UP) && sel > 0) sel--;
        if (pressed & BTN_CIRCLE) result = 1;
        if (pressed & BTN_CROSS) {
            switch (sel) {
            case M_RESUME: result = 1; break;
            case M_SAVE:
                if (state_save()) { snprintf(note, sizeof note, "Estado guardado"); has_state = 1; }
                else snprintf(note, sizeof note, "No se pudo guardar el estado");
                break;
            case M_LOAD:
                if (state_load()) result = 1;
                else snprintf(note, sizeof note, has_state ? "No se pudo cargar el estado" : "Todav\xC3\xAD" "a no hay un estado guardado");
                break;
            case M_RESET: CORE(retro_reset)(); result = 1; break;
            case M_QUIT: result = 0; break;
            }
        }
        canvas_blit(&c, &snap);
        float w = 640, h = 140 + M_COUNT * 72 + 60, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
        shadow_rrect(&c, x, y + 24, w, h, 28, 70, 0.7f);
        fill_rrect(&c, x, y, w, h, 28, C_PANEL, 0.97f);
        stroke_rrect(&c, x, y, w, h, 28, 1.5f, 0xFFFFFF, 0.1f);
        float tx = x + 48;
        float wx = tx;
        wx += draw_text(&c, FONT_MONO, 18, wx, y + 62, "SAM", C_ACCENT, 1, 3.6f);
        wx += draw_text(&c, FONT_MONO, 18, wx, y + 62, "PSP", C_BRAND, 1, 3.6f);
        draw_text(&c, FONT_MONO, 18, wx, y + 62, "4 \xC2\xB7 PS1", C_ACCENT, 1, 3.6f);
        char t[160];
        text_ellipsize(FONT_DISPLAY, 34, title, w - 96, t, sizeof t, -0.5f);
        draw_text(&c, FONT_DISPLAY, 34, tx, y + 110, t, C_TEXT, 1, -0.5f);
        for (int i = 0; i < M_COUNT; i++) {
            float ry = y + 140 + i * 72;
            if (i == sel) {
                fill_rrect(&c, x + 24, ry, w - 48, 60, 16, C_ACCENT, 0.14f);
                stroke_rrect(&c, x + 24, ry, w - 48, 60, 16, 2, C_ACCENT, 0.8f);
            }
            uint32_t col = (i == M_LOAD && !has_state) ? C_MUTED : C_TEXT;
            draw_text(&c, FONT_DISPLAY, 26, tx, ry + 40, items[i], col, 1, 0);
        }
        draw_text(&c, FONT_BODY, 20, tx, y + h - 26, note[0] ? note : "Cruz: elegir   \xC2\xB7   C\xC3\xADrculo: volver al juego", note[0] ? C_ACCENT : C_MUTED, 1, 0);
        plat_present(&c);
    }
    free(c.px);
    free(snap.px);
    // Repaint the side bars of every buffer after the menu covered them.
    S.borders_left = 3;
    return result;
}

// ---------------------------------------------------------------- run

static void title_from_path(const char *path, char *out, size_t out_len) {
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    if (strcasecmp(name, "EBOOT.PBP") == 0 && slash) {   // name it after its folder
        const char *p = slash;
        while (p > path && p[-1] != '/') p--;
        snprintf(out, out_len, "%.*s", (int)(slash - p), p);
    } else {
        snprintf(out, out_len, "%s", name);
        char *dot = strrchr(out, '.');
        if (dot) *dot = 0;
    }
}

int ps1_run(const char *path) {
    memset(&S, 0, sizeof S);
    S.fmt = RETRO_PIXEL_FORMAT_0RGB1555;
    snprintf(S.sys_dir, sizeof S.sys_dir, PS1_ROOT "/bios");
    snprintf(S.save_dir, sizeof S.save_dir, PS1_ROOT "/saves");
    mkdirs(S.sys_dir);
    mkdirs(S.save_dir);
    S.log = fopen(PS1_ROOT "/ps1.log", "w");
    title_from_path(path, g_base, sizeof g_base);
    slog("SAMPSP4 PS1: %s", path);

    CORE(retro_set_environment)(env);
    CORE(retro_set_video_refresh)(video_refresh);
    CORE(retro_set_audio_sample)(audio_sample);
    CORE(retro_set_audio_sample_batch)(audio_batch);
    CORE(retro_set_input_poll)(input_poll);
    CORE(retro_set_input_state)(input_state);
    CORE(retro_init)();

    struct retro_game_info info = { .path = path };
    if (!CORE(retro_load_game)(&info)) {
        slog("the core could not load the game");
        CORE(retro_deinit)();
        if (S.log) fclose(S.log);
        return -1;
    }
    CORE(retro_set_controller_port_device)(0, RETRO_DEVICE_JOYPAD);
    struct retro_system_av_info av;
    CORE(retro_get_system_av_info)(&av);
    S.sample_rate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100;
    slog("timing %.3f fps, %.0f Hz", av.timing.fps, S.sample_rate);
    card_load();
    plat_audio_open();

    uint64_t last_card = plat_time_us();
    uint32_t prev = 0;
    int frames = 0;
    // Performance, logged every 5 s: where each frame's time goes.
    uint64_t perf_t = plat_time_us(), run_us = 0;
    int perf_frames = 0;
    PlatStats ps0 = plat_stats;
    uint64_t blit0 = S.blit_us, drop0 = S.dropped;
    for (;;) {
        uint64_t r0 = plat_time_us();
        CORE(retro_run)();
        run_us += plat_time_us() - r0;
        frames++;
        perf_frames++;
        uint64_t pt = plat_time_us();
        if (pt - perf_t >= 5000000) {
            double secs = (pt - perf_t) / 1e6, n = perf_frames ? perf_frames : 1;
            double aw = (plat_stats.audio_wait_us - ps0.audio_wait_us) / 1000.0 / n;
            double fw = (plat_stats.flip_wait_us - ps0.flip_wait_us) / 1000.0 / n;
            double bl = (S.blit_us - blit0) / 1000.0 / n;
            double emu = run_us / 1000.0 / n - aw - fw - bl;
            slog("perf: %.1f fps | emu %.1f ms | imagen %.1f ms | espera audio %.1f ms | espera pantalla %.1f ms | "
                 "sin mostrar %llu | huecos audio %llu | audio tirado %llu",
                 perf_frames / secs, emu, bl, aw, fw, (unsigned long long)(S.dropped - drop0),
                 (unsigned long long)(plat_stats.underruns - ps0.underruns),
                 (unsigned long long)(plat_stats.audio_dropped - ps0.audio_dropped));
            perf_t = pt; run_us = 0; perf_frames = 0; ps0 = plat_stats; blit0 = S.blit_us; drop0 = S.dropped;
        }
        uint32_t b = S.pad.buttons;
        uint32_t pressed = b & ~prev;
        prev = b;
        int menu = ((b & (BTN_L3 | BTN_R3)) == (BTN_L3 | BTN_R3) && (pressed & (BTN_L3 | BTN_R3))) ||
                   ((b & (BTN_TOUCH | BTN_OPTIONS)) == (BTN_TOUCH | BTN_OPTIONS) && (pressed & (BTN_TOUCH | BTN_OPTIONS)));
#ifdef VAULT_HOST
        if (plat_host_done()) break;
#endif
        if (menu) {
            card_save(0);
            if (!pause_menu(g_base)) break;
            prev = ~0u;
        }
        uint64_t now = plat_time_us();
        if (now - last_card > 5000000) { card_save(0); last_card = now; }
    }
    if (perf_frames > 30) {
        double n = perf_frames, secs = (plat_time_us() - perf_t) / 1e6;
        slog("perf (al salir): %.1f fps | %.1f ms por cuadro | sin mostrar %llu", n / secs, run_us / 1000.0 / n,
             (unsigned long long)(S.dropped - drop0));
    }
    slog("quit after %d frames", frames);
    card_save(0);
    plat_audio_close();
    CORE(retro_unload_game)();
    CORE(retro_deinit)();
    free(g_card_shadow);
    g_card_shadow = NULL;
    if (S.log) fclose(S.log);
    return 0;
}

#ifdef VAULT_EMBEDDED
int vault_ps1_main(const char *path) {
    if (!plat_init()) return -1;
    int rc = ps1_run(path);
    plat_shutdown();
    return rc;
}
#endif
