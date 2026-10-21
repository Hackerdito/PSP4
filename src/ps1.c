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
    int vars_dirty;                    // settings changed: the core re-reads its options
    uint16_t rumble[2];                // strong / weak motor, from the core
    uint8_t rumble_sent[2];
    int last_rect[4];                  // output rectangle last drawn (to repaint the bars)
    Canvas fps_box;                    // the FPS counter, drawn once a second
    uint64_t fps_t;
    int fps_frames;
    int redraw;                        // buffers to repaint with the last frame (after the menu)
} S;

// ---------------------------------------------------------------- settings (all PS1 games)

typedef struct {
    int size;       // 0 4:3, 1 full screen (16:9), 2 pixel perfect (whole multiples)
    int filter;     // 0 sharp, 1 smooth (bilinear), 2 TV scanlines
    int hires;      // the GPU's 2x internal resolution
    int fps;        // FPS counter on screen
    int pad;        // 0 digital pad, 1 DualShock (analog sticks)
    int rumble;     // vibration
    int frameskip;  // skip frames when a game runs slow
} Ps1Settings;

static Ps1Settings G = { 0, 0, 0, 0, 0, 1, 0 };
#define SETTINGS_FILE PS1_ROOT "/ajustes.txt"

static void settings_load(void) {
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char key[32];
    int v;
    while (fscanf(f, "%31[^=]=%d\n", key, &v) == 2) {
        if (!strcmp(key, "pantalla")) G.size = v % 3;
        else if (!strcmp(key, "filtro")) G.filter = v % 3;
        else if (!strcmp(key, "alta_resolucion")) G.hires = !!v;
        else if (!strcmp(key, "fps")) G.fps = !!v;
        else if (!strcmp(key, "control")) G.pad = !!v;
        else if (!strcmp(key, "vibracion")) G.rumble = !!v;
        else if (!strcmp(key, "saltar_cuadros")) G.frameskip = !!v;
    }
    fclose(f);
}

static void settings_save(void) {
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "pantalla=%d\nfiltro=%d\nalta_resolucion=%d\nfps=%d\ncontrol=%d\nvibracion=%d\nsaltar_cuadros=%d\n",
            G.size, G.filter, G.hires, G.fps, G.pad, G.rumble, G.frameskip);
    fclose(f);
}

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
        { "pcsx_rearmed_vibration", "enabled" },
        { "pcsx_rearmed_spu_interpolation", "simple" },
        { "pcsx_rearmed_dithering", "enabled" },
    };
#ifdef VAULT_HOST
    if (strcmp(key, "pcsx_rearmed_drc") == 0 && getenv("PS1_INTERPRETER")) return "disabled";
#endif
    // From the player's settings (pause menu).
    if (!strcmp(key, "pcsx_rearmed_neon_enhancement_enable")) return G.hires ? "enabled" : "disabled";
    if (!strcmp(key, "pcsx_rearmed_frameskip_type")) return G.frameskip ? "auto" : "disabled";
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

static bool RETRO_CALLCONV set_rumble(unsigned port, enum retro_rumble_effect effect, uint16_t strength) {
    if (port == 0 && effect <= RETRO_RUMBLE_WEAK) S.rumble[effect] = strength;
    return true;
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
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = S.vars_dirty; S.vars_dirty = 0; return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
        ((struct retro_rumble_interface *)data)->set_rumble_state = set_rumble;
        return true;
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

// Where the game goes on the 1920x1080 screen, for the chosen size.
static void out_rect(int *ox, int *oy, int *ow, int *oh) {
    switch (G.size) {
    case 1: *ox = 0; *oy = 0; *ow = SCREEN_W; *oh = SCREEN_H; break;
    case 2: {   // whole multiples of the game's lines (480-line modes count as 240)
        int base = S.fh >= 400 ? (int)S.fh / 2 : (int)S.fh;
        if (base <= 0) base = 240;
        int k = SCREEN_H / base;
        if (k < 1) k = 1;
        *oh = base * k;
        *ow = *oh * 4 / 3;
        *ox = (SCREEN_W - *ow) / 2;
        *oy = (SCREEN_H - *oh) / 2;
        break;
    }
    default: *ow = 1440; *oh = 1080; *ox = (SCREEN_W - 1440) / 2; *oy = 0; break;
    }
}

#define SRC_MAX 2048   // widest frame (640 x 2 with the high-resolution mode)

static void convert_row(unsigned sy, uint32_t *row) {
    const uint8_t *src = (const uint8_t *)S.frame + (size_t)sy * S.pitch;
    unsigned w = S.fw > SRC_MAX ? SRC_MAX : S.fw;
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
}

static inline uint32_t lerp_px(uint32_t a, uint32_t b, uint32_t w) {   // w: 0..256
    uint32_t rb = (((a & 0xFF00FFu) * (256 - w) + (b & 0xFF00FFu) * w) >> 8) & 0xFF00FFu;
    uint32_t g = (((a & 0x00FF00u) * (256 - w) + (b & 0x00FF00u) * w) >> 8) & 0x00FF00u;
    return 0xFF000000u | rb | g;
}

// Scales the core's frame into `dst` (row stride `stride` pixels) with the chosen size and
// filter. Rows are built in ordinary (cached) memory and only written to `dst`: the screen's
// memory is write-combined, and reading it back took ~60 ms a frame.
static void blit_frame(uint32_t *dst, int stride) {
    if (!S.frame || !S.fw || !S.fh) return;
    int ox, oy, ow, oh;
    out_rect(&ox, &oy, &ow, &oh);
    static int xmap[SCREEN_W], x1map[SCREEN_W];
    static uint16_t xw[SCREEN_W];
    static unsigned map_fw;
    static int map_ow, map_filter = -1;
    if (map_fw != S.fw || map_ow != ow || map_filter != G.filter) {
        for (int x = 0; x < ow; x++) {
            xmap[x] = (int)((uint64_t)x * S.fw / (unsigned)ow);
            int64_t pos = ((int64_t)(2 * x + 1) * S.fw * 256) / (2 * ow) - 128;   // centre-aligned, 8.8
            if (pos < 0) pos = 0;
            int x0 = (int)(pos >> 8);
            x1map[x] = x0 + 1 < (int)S.fw ? x0 + 1 : x0;
            xw[x] = (uint16_t)(pos & 255);
            if (G.filter == 1) xmap[x] = x0;
        }
        map_fw = S.fw; map_ow = ow; map_filter = G.filter;
    }

    static uint32_t row[SRC_MAX], line[SCREEN_W], dark[SCREEN_W];
    if (G.filter == 1) {
        // Smooth: bilinear. Two horizontally-scaled source rows are kept and blended per line.
        static uint32_t ha[SCREEN_W], hb[SCREEN_W];
        int ia = -1, ib = -1;
        for (int y = 0; y < oh; y++) {
            int64_t pos = ((int64_t)(2 * y + 1) * S.fh * 256) / (2 * oh) - 128;
            if (pos < 0) pos = 0;
            int s0 = (int)(pos >> 8), s1 = s0 + 1 < (int)S.fh ? s0 + 1 : s0;
            uint32_t wv = (uint32_t)(pos & 255);
            if (ia != s0) {
                if (ib == s0) { memcpy(ha, hb, (size_t)ow * 4); }
                else { convert_row((unsigned)s0, row); for (int x = 0; x < ow; x++) ha[x] = lerp_px(row[xmap[x]], row[x1map[x]], xw[x]); }
                ia = s0; ib = -1;
            }
            if (ib != s1) {
                convert_row((unsigned)s1, row);
                for (int x = 0; x < ow; x++) hb[x] = lerp_px(row[xmap[x]], row[x1map[x]], xw[x]);
                ib = s1;
            }
            for (int x = 0; x < ow; x++) line[x] = lerp_px(ha[x], hb[x], wv);
            memcpy(dst + (size_t)(oy + y) * stride + ox, line, (size_t)ow * 4);
        }
        return;
    }
    // Sharp (nearest) and TV (nearest + darker gap between the game's lines).
    unsigned lines = S.fh >= 400 ? S.fh / 2 : S.fh;   // scanlines follow the TV's 240 lines
    int prev_sy = -1;
    for (int y = 0; y < oh; y++) {
        int sy = (int)((uint64_t)y * S.fh / (unsigned)oh);
        if (sy != prev_sy) {
            prev_sy = sy;
            convert_row((unsigned)sy, row);
            for (int x = 0; x < ow; x++) line[x] = row[xmap[x]];
            if (G.filter == 2)
                for (int x = 0; x < ow; x++) { uint32_t q = line[x]; dark[x] = 0xFF000000u | (((q >> 1) & 0x7F7F7F) + ((q >> 3) & 0x1F1F1F)); }
        }
        const uint32_t *src = line;
        if (G.filter == 2) {
            uint64_t phase = ((uint64_t)y * lines * 256 / (unsigned)oh) & 255;   // position inside a TV line
            if (phase >= 150) src = dark;
        }
        memcpy(dst + (size_t)(oy + y) * stride + ox, src, (size_t)ow * 4);
    }
}

// Black everywhere outside the game's rectangle (write-only).
static void clear_outside(uint32_t *dst, int stride) {
    int ox, oy, ow, oh;
    out_rect(&ox, &oy, &ow, &oh);
    static uint32_t black[SCREEN_W];
    if (!black[0]) for (int i = 0; i < SCREEN_W; i++) black[i] = 0xFF000000u;
    for (int y = 0; y < SCREEN_H; y++) {
        uint32_t *l = dst + (size_t)y * stride;
        if (y < oy || y >= oy + oh) { memcpy(l, black, SCREEN_W * 4); continue; }
        if (ox > 0) { memcpy(l, black, (size_t)ox * 4); memcpy(l + ox + ow, black, (size_t)(SCREEN_W - ox - ow) * 4); }
    }
}

// FPS counter: redrawn once a second into a small canvas, copied into the corner each frame.
static void draw_fps(uint32_t *dst, int stride) {
    S.fps_frames++;
    uint64_t t = plat_time_us();
    int fresh = !S.fps_box.px;
    if (fresh) { S.fps_box = canvas_new(150, 44); S.fps_t = t; S.fps_frames = 0; }
    if (fresh || t - S.fps_t >= 1000000) {
        char txt[24];
        if (fresh) snprintf(txt, sizeof txt, "-- fps");
        else snprintf(txt, sizeof txt, "%.0f fps", S.fps_frames * 1e6 / (double)(t - S.fps_t));
        for (int i = 0; i < 150 * 44; i++) S.fps_box.px[i] = 0xFF000000u;
        fill_rrect(&S.fps_box, 0, 0, 150, 44, 10, 0x11131A, 1);
        draw_text(&S.fps_box, FONT_MONO, 22, 16, 30, txt, 0x3DF5C8, 1, 0);
        S.fps_t = t;
        S.fps_frames = 0;
    }
    for (int y = 0; y < 44; y++) memcpy(dst + (size_t)(20 + y) * stride + 24, S.fps_box.px + y * 150, 150 * 4);
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) {
        // Duplicate frame: the screen keeps the last one, unless the menu covered it (then the
        // last frame, which PCSX keeps, is drawn again into each buffer).
        if (S.redraw <= 0 || !S.frame) return;
        S.redraw--;
    } else {
        S.frame = data; S.fw = w; S.fh = h; S.pitch = pitch;
    }
    if (w != S.last_w || h != S.last_h) {
        slog("video %ux%u", w, h);
        S.last_w = w; S.last_h = h;
        S.borders_left = 3;
    }
    if (!plat_frame_ready()) { S.dropped++; return; }   // screen still busy: skip, don't wait
    uint32_t *fb = plat_frame_begin();
    int r[4];
    out_rect(&r[0], &r[1], &r[2], &r[3]);
    if (memcmp(r, S.last_rect, sizeof r)) { memcpy(S.last_rect, r, sizeof r); S.borders_left = 3; }
    if (S.borders_left > 0) { clear_outside(fb, SCREEN_W); S.borders_left--; }
    uint64_t t = plat_time_us();
    blit_frame(fb, SCREEN_W);
    if (G.fps) draw_fps(fb, SCREEN_W);
    else if (S.fps_box.px) { S.borders_left = 3; free(S.fps_box.px); S.fps_box.px = NULL; }
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
    // On the digital pad the left stick doubles as the d-pad; on the DualShock it's a stick.
    if (!G.pad) {
        if (S.pad.lx < 48) b |= BTN_LEFT;
        if (S.pad.lx > 208) b |= BTN_RIGHT;
        if (S.pad.ly < 48) b |= BTN_UP;
        if (S.pad.ly > 208) b |= BTN_DOWN;
    }
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

static void apply_pad(void) {
    CORE(retro_set_controller_port_device)(0, G.pad ? RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1) : RETRO_DEVICE_JOYPAD);
}

static void apply_rumble(void) {
    uint8_t big = G.rumble ? (uint8_t)(S.rumble[RETRO_RUMBLE_STRONG] >> 8) : 0;
    uint8_t small = G.rumble ? (uint8_t)(S.rumble[RETRO_RUMBLE_WEAK] >> 8) : 0;
    if (big != S.rumble_sent[0] || small != S.rumble_sent[1]) {
        plat_rumble(big, small);
        S.rumble_sent[0] = big; S.rumble_sent[1] = small;
    }
}

// The game frozen behind the menu: drawn with the current size and filter, then dimmed.
static void paint_snapshot(Canvas *snap) {
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) snap->px[i] = 0xFF000000u;
    blit_frame(snap->px, SCREEN_W);
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) {
        uint32_t q = snap->px[i];
        snap->px[i] = ((q >> 2) & 0x3F3F3F) + ((q >> 3) & 0x1F1F1F);
    }
}

static void draw_glyph(Canvas *c, int g, float cx, float cy, float r) {   // 0 X, 1 O, 2 triangle, 3 square
    static const uint32_t col[4] = { 0x6FA8FF, 0xFF6B6B, 0x3DF5C8, 0xF49BFF };
    uint32_t k = col[g];
    stroke_circle(c, cx, cy, r - 1, 2, k, 1);
    float q = r * 0.42f;
    if (g == 0) { draw_line(c, cx - q, cy - q, cx + q, cy + q, 2.2f, k, 1); draw_line(c, cx + q, cy - q, cx - q, cy + q, 2.2f, k, 1); }
    else if (g == 1) stroke_circle(c, cx, cy, q, 2.2f, k, 1);
    else if (g == 2) { float h = q * 1.1f; draw_line(c, cx, cy - h, cx + h, cy + h * 0.75f, 2.2f, k, 1); draw_line(c, cx + h, cy + h * 0.75f, cx - h, cy + h * 0.75f, 2.2f, k, 1); draw_line(c, cx - h, cy + h * 0.75f, cx, cy - h, 2.2f, k, 1); }
    else stroke_rrect(c, cx - q, cy - q, q * 2, q * 2, 1, 2.2f, k, 1);
}

static float chip(Canvas *c, float x, float cy, const char *t) {
    float w = text_width(FONT_MONO, 16, t, 0) + 18;
    stroke_rrect(c, x, cy - 15, w, 30, 7, 1.5f, C_MUTED, 0.8f);
    draw_text(c, FONT_MONO, 16, x + 9, cy + 6, t, C_TEXT, 1, 0);
    return w;
}

// "Ver controles": what each DualShock 4 button does in PS1 games.
static void controls_page(Canvas *c, const Canvas *snap) {
    PadState p;
    uint32_t prev = ~0u;
    for (;;) {
        plat_pad(&p);
        uint32_t pressed = p.buttons & ~prev;
        prev = p.buttons;
        if (pressed & (BTN_CIRCLE | BTN_CROSS)) return;
#ifdef VAULT_HOST
        if (plat_host_done()) return;
#endif
        canvas_blit(c, snap);
        float w = 980, h = 760, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
        shadow_rrect(c, x, y + 24, w, h, 28, 70, 0.7f);
        fill_rrect(c, x, y, w, h, 28, C_PANEL, 0.98f);
        stroke_rrect(c, x, y, w, h, 28, 1.5f, 0xFFFFFF, 0.1f);
        float tx = x + 52;
        draw_text(c, FONT_MONO, 18, tx, y + 62, "CONTROLES \xC2\xB7 PS1", C_ACCENT, 1, 3.6f);
        draw_text(c, FONT_DISPLAY, 34, tx, y + 110, G.pad ? "Control DualShock (anal\xC3\xB3gico)" : "Control digital", C_TEXT, 1, -0.5f);
        struct { const char *k; int glyphs; const char *v; } rows[] = {
            { "\xE2\x9C\x95  \xE2\x97\x8B  \xE2\x96\xA1  \xE2\x96\xB3", 1, "Igual que en el control de PS1" },
            { "Cruceta", 0, "Cruceta" },
            { G.pad ? "Sticks" : "Stick izquierdo", 0, G.pad ? "Sticks anal\xC3\xB3gicos (juegos DualShock)" : "Tambi\xC3\xA9n mueve como la cruceta" },
            { "L1  R1  L2  R2", 0, "L1  R1  L2  R2" },
            { "OPTIONS", 0, "START" },
            { "Clic en el touchpad", 0, "SELECT" },
            { G.pad ? "L3  R3 (uno)" : "L3  R3", 0, G.pad ? "L3  R3 de los sticks" : "No se usan" },
            { "L3 + R3 juntos", 0, "Este men\xC3\xBA (o touchpad + OPTIONS)" },
        };
        float ry = y + 170;
        for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            fill_rrect(c, x + 28, ry - 34, w - 56, 54, 12, 0xFFFFFF, i % 2 ? 0.0f : 0.035f);
            if (rows[i].glyphs) for (int g = 0; g < 4; g++) draw_glyph(c, g, tx + 16 + g * 44, ry - 7, 15);
            else draw_text(c, FONT_DISPLAY, 24, tx, ry, rows[i].k, C_TEXT, 1, 0);
            draw_text(c, FONT_BODY, 23, x + 430, ry, rows[i].v, C_MUTED, 1, 0);
            ry += 62;
        }
        draw_text(c, FONT_BODY, 20, tx, y + h - 30, "Cambia el tipo de control en el men\xC3\xBA: Control > Tipo de control.", C_MUTED, 1, 0);
        plat_present(c);
    }
}

enum { R_ACTION, R_CHOICE, R_HEADER };
enum { A_RESUME, A_SAVE, A_LOAD, A_CONTROLS, A_RESET, A_QUIT };

// Returns 1 to keep playing, 0 to quit.
static int pause_menu(const char *title) {
    static const char *kNo[2] = { "No", "S\xC3\xAD" };
    static const char *kSize[3] = { "4:3 original", "Pantalla completa", "Pixel perfecto" };
    static const char *kFilter[3] = { "N\xC3\xADtido", "Suave", "TV (scanlines)" };
    static const char *kPad[2] = { "Digital", "DualShock" };
    struct Row { int kind; const char *label; int id; int *val; int n; const char **names; } rows[] = {
        { R_ACTION, "Continuar", A_RESUME, NULL, 0, NULL },
        { R_ACTION, "Guardar estado", A_SAVE, NULL, 0, NULL },
        { R_ACTION, "Cargar estado", A_LOAD, NULL, 0, NULL },
        { R_HEADER, "PANTALLA", 0, NULL, 0, NULL },
        { R_CHOICE, "Tama\xC3\xB1o", 0, &G.size, 3, kSize },
        { R_CHOICE, "Filtro", 0, &G.filter, 3, kFilter },
        { R_CHOICE, "Alta resoluci\xC3\xB3n", 0, &G.hires, 2, kNo },
        { R_CHOICE, "Mostrar FPS", 0, &G.fps, 2, kNo },
        { R_HEADER, "CONTROL", 0, NULL, 0, NULL },
        { R_CHOICE, "Tipo de control", 0, &G.pad, 2, kPad },
        { R_CHOICE, "Vibraci\xC3\xB3n", 0, &G.rumble, 2, kNo },
        { R_ACTION, "Ver controles", A_CONTROLS, NULL, 0, NULL },
        { R_HEADER, "JUEGO", 0, NULL, 0, NULL },
        { R_CHOICE, "Saltar cuadros si va lento", 0, &G.frameskip, 2, kNo },
        { R_ACTION, "Reiniciar juego", A_RESET, NULL, 0, NULL },
        { R_ACTION, "Salir a SAMPSP4", A_QUIT, NULL, 0, NULL },
    };
    const int nrows = (int)(sizeof rows / sizeof rows[0]);
    plat_rumble(0, 0);
    S.rumble_sent[0] = S.rumble_sent[1] = 0;
    Canvas snap = canvas_new(SCREEN_W, SCREEN_H);
    paint_snapshot(&snap);
    Canvas c = canvas_new(SCREEN_W, SCREEN_H);
    char note[120] = "";
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
        if (p.lx < 48) held |= BTN_LEFT;
        if (p.lx > 208) held |= BTN_RIGHT;
        uint32_t pressed = held & ~prev;
        prev = held;
        if (pressed & BTN_DOWN) do sel = (sel + 1) % nrows; while (rows[sel].kind == R_HEADER);
        if (pressed & BTN_UP) do sel = (sel + nrows - 1) % nrows; while (rows[sel].kind == R_HEADER);
        if (pressed & BTN_CIRCLE) result = 1;
#ifdef VAULT_HOST
        if (plat_host_done()) result = 0;
#endif
        struct Row *r = &rows[sel];
        int step = (pressed & BTN_RIGHT) ? 1 : (pressed & BTN_LEFT) ? -1 : (r->kind == R_CHOICE && (pressed & BTN_CROSS)) ? 1 : 0;
        if (r->kind == R_CHOICE && step) {
            *r->val = (*r->val + step + r->n) % r->n;
            settings_save();
            note[0] = 0;
            if (r->val == &G.size || r->val == &G.filter) paint_snapshot(&snap);
            if (r->val == &G.hires) { S.vars_dirty = 1; snprintf(note, sizeof note, "Se aplica al volver al juego"); }
            if (r->val == &G.frameskip) S.vars_dirty = 1;
            if (r->val == &G.pad) apply_pad();
        }
        if (r->kind == R_ACTION && (pressed & BTN_CROSS)) {
            switch (r->id) {
            case A_RESUME: result = 1; break;
            case A_SAVE:
                if (state_save()) { snprintf(note, sizeof note, "Estado guardado"); has_state = 1; }
                else snprintf(note, sizeof note, "No se pudo guardar el estado");
                break;
            case A_LOAD:
                if (state_load()) result = 1;
                else snprintf(note, sizeof note, has_state ? "No se pudo cargar el estado" : "Todav\xC3\xAD" "a no hay un estado guardado");
                break;
            case A_CONTROLS: controls_page(&c, &snap); prev = ~0u; break;
            case A_RESET: CORE(retro_reset)(); result = 1; break;
            case A_QUIT: result = 0; break;
            }
        }

        canvas_blit(&c, &snap);
        const float row_h = 50, head_h = 38;
        float content = 0;
        for (int i = 0; i < nrows; i++) content += rows[i].kind == R_HEADER ? head_h : row_h;
        float w = 780, h = 140 + content + 70, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2;
        shadow_rrect(&c, x, y + 24, w, h, 28, 70, 0.7f);
        fill_rrect(&c, x, y, w, h, 28, C_PANEL, 0.97f);
        stroke_rrect(&c, x, y, w, h, 28, 1.5f, 0xFFFFFF, 0.1f);
        float tx = x + 48, wx = tx;
        wx += draw_text(&c, FONT_MONO, 18, wx, y + 58, "SAM", C_ACCENT, 1, 3.6f);
        wx += draw_text(&c, FONT_MONO, 18, wx, y + 58, "PSP", C_BRAND, 1, 3.6f);
        draw_text(&c, FONT_MONO, 18, wx, y + 58, "4 \xC2\xB7 PS1", C_ACCENT, 1, 3.6f);
        char t[160];
        text_ellipsize(FONT_DISPLAY, 32, title, w - 96, t, sizeof t, -0.5f);
        draw_text(&c, FONT_DISPLAY, 32, tx, y + 104, t, C_TEXT, 1, -0.5f);
        float ry = y + 130;
        for (int i = 0; i < nrows; i++) {
            struct Row *q = &rows[i];
            if (q->kind == R_HEADER) {
                draw_text(&c, FONT_MONO, 15, tx, ry + 28, q->label, C_MUTED, 1, 3);
                ry += head_h;
                continue;
            }
            if (i == sel) {
                fill_rrect(&c, x + 24, ry, w - 48, row_h - 6, 14, C_ACCENT, 0.14f);
                stroke_rrect(&c, x + 24, ry, w - 48, row_h - 6, 14, 2, C_ACCENT, 0.8f);
            }
            uint32_t col = (q->id == A_LOAD && q->kind == R_ACTION && !has_state) ? C_MUTED : C_TEXT;
            draw_text(&c, FONT_DISPLAY, 23, tx, ry + 31, q->label, col, 1, 0);
            if (q->kind == R_CHOICE) {
                const char *v = q->names[*q->val];
                float vw = text_width(FONT_BODY, 22, v, 0);
                float vx = x + w - 48 - vw - (i == sel ? 30 : 0);
                draw_text(&c, FONT_BODY, 22, vx, ry + 30, v, i == sel ? C_ACCENT : C_MUTED, 1, 0);
                if (i == sel) {
                    fill_triangle(&c, vx - 22, ry + 22, vx - 12, ry + 15, vx - 12, ry + 29, C_ACCENT, 1);
                    fill_triangle(&c, x + w - 56, ry + 22, x + w - 66, ry + 15, x + w - 66, ry + 29, C_ACCENT, 1);
                }
            }
            ry += row_h;
        }
        float hy = y + h - 30, hx = tx;
        if (note[0]) draw_text(&c, FONT_BODY, 20, hx, hy, note, C_ACCENT, 1, 0);
        else {
            draw_glyph(&c, 0, hx + 12, hy - 7, 12); hx += draw_text(&c, FONT_BODY, 19, hx + 30, hy, "Elegir", C_MUTED, 1, 0) + 54;
            hx += chip(&c, hx, hy - 7, "\xE2\x86\x90 \xE2\x86\x92") + 10; hx += draw_text(&c, FONT_BODY, 19, hx, hy, "Cambiar", C_MUTED, 1, 0) + 40;
            draw_glyph(&c, 1, hx + 12, hy - 7, 12); draw_text(&c, FONT_BODY, 19, hx + 30, hy, "Volver al juego", C_MUTED, 1, 0);
        }
        plat_present(&c);
    }
    free(c.px);
    free(snap.px);
    // Repaint every buffer (bars and game) after the menu covered them.
    S.borders_left = 3;
    S.redraw = 3;
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
    settings_load();
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
    apply_pad();
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
        apply_rumble();
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
    plat_rumble(0, 0);
    if (S.fps_box.px) { free(S.fps_box.px); S.fps_box.px = NULL; }
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
