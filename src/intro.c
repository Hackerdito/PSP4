// intro.c - SAMPSP4's start-up sequence before a game: the wordmark rising out of a blue light
// sweep, with a short synthesized chime (original; no console's boot logo or sound). ~2.4 s,
// skippable with X / O.
#include "intro.h"

#include "gfx.h"
#include "platform.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define DURATION 2.4f
#define C_BLUE  0x3F7BFF
#define C_WHITE 0xF2F4F8

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }   // ease-out cubic

// The chime: a soft swell of filtered noise, then two bell tones (E5, B5) with a little shimmer.
static int16_t *make_chime(int *frames_out) {
    int n = (int)(DURATION * RATE);
    int16_t *pcm = calloc((size_t)n * 2, sizeof(int16_t));
    uint32_t seed = 0x9E3779B9u;
    float lp = 0, lp2 = 0;
    for (int i = 0; i < n; i++) {
        float t = (float)i / RATE;
        // Swell: noise through two one-pole low-passes, rising over 0.5 s and cut at the bell.
        seed = seed * 1664525u + 1013904223u;
        float white = ((seed >> 9) / 4194304.0f) - 1.0f;
        float cut = 0.02f + 0.10f * clamp01(t / 0.55f);
        lp += (white - lp) * cut;
        lp2 += (lp - lp2) * cut;
        float swell = lp2 * 2.2f * clamp01(t / 0.5f) * (t < 0.55f ? 1 : expf(-(t - 0.55f) * 9));
        float l = swell, r = swell;
        // Bells: fundamental + inharmonic partials, exponential decay; B5 slightly right.
        static const struct { float at, f, pan; } notes[] = { { 0.55f, 659.26f, -0.25f }, { 0.78f, 987.77f, 0.3f } };
        for (int k = 0; k < 2; k++) {
            float u = t - notes[k].at;
            if (u < 0) continue;
            float env = expf(-u * 3.2f) * (1 - expf(-u * 400));
            float w = 2 * 3.14159265f * notes[k].f * u;
            float v = (sinf(w) + 0.45f * sinf(w * 2.76f) * expf(-u * 6) + 0.25f * sinf(w * 5.4f) * expf(-u * 11) +
                       0.12f * sinf(w * 0.5f)) * env * 0.26f;
            l += v * (1 - notes[k].pan);
            r += v * (1 + notes[k].pan);
        }
        if (t > DURATION - 0.3f) { float f = (DURATION - t) / 0.3f; l *= f; r *= f; }
        int sl = (int)(l * 26000), sr = (int)(r * 26000);
        pcm[i * 2] = (int16_t)(sl > 32767 ? 32767 : sl < -32768 ? -32768 : sl);
        pcm[i * 2 + 1] = (int16_t)(sr > 32767 ? 32767 : sr < -32768 ? -32768 : sr);
    }
    *frames_out = n;
    return pcm;
}

// Background: near black with blue light from below, and two thin arcs of light.
static void paint_background(Canvas *bg) {
    paint_glow_background(bg, 0x040508, 0x1B3FCC, 0.55f, 0x3F7BFF, 0.22f);
    for (int arc = 0; arc < 3; arc++) {
        float y0 = SCREEN_H * (0.94f + arc * 0.04f), sag = SCREEN_H * (0.16f - arc * 0.03f);
        float px = 0, py = y0 - sag;
        for (int i = 1; i <= 96; i++) {
            float u = i / 96.0f, x = u * SCREEN_W;
            float y = y0 - sag * (1 - 4 * (u - 0.5f) * (u - 0.5f)) - (u - 0.5f) * 80 * arc;
            float a = (arc == 0 ? 0.55f : 0.22f) * sinf(u * 3.14159265f);
            draw_line(bg, px, py, x, y, arc == 0 ? 3.0f : 2.0f, arc == 0 ? 0x6E9BFF : 0x3F7BFF, a);
            px = x; py = y;
        }
    }
}

void intro_play(const char *system_label, const char *title) {
    Canvas bg = canvas_new(SCREEN_W, SCREEN_H);
    Canvas c = canvas_new(SCREEN_W, SCREEN_H);
    paint_background(&bg);
    int chime_frames = 0;
    int16_t *chime = make_chime(&chime_frames);
    int audio = plat_audio_open();
    int pushed = 0;

    const float size = 128, track = 10;
    float w_sam = text_width(FONT_DISPLAY, size, "SAM", track), w_psp = text_width(FONT_DISPLAY, size, "PSP", track);
    float w_all = w_sam + w_psp + text_width(FONT_DISPLAY, size, "4", track);
    float x0 = (SCREEN_W - w_all) / 2, base = SCREEN_H * 0.5f + 30;

#ifndef VAULT_HOST
    uint64_t start = plat_time_us();
#endif
    int frame = 0;
    uint32_t prev = ~0u;
    for (;;) {
#ifdef VAULT_HOST
        float t = frame / 60.0f;   // host: fixed steps, for repeatable screenshots
#else
        float t = (plat_time_us() - start) / 1e6f;
#endif
        frame++;
        if (t >= DURATION) break;
        uint32_t held = plat_buttons();
        if ((held & ~prev) & (BTN_CROSS | BTN_CIRCLE)) break;
        prev = held;

        // Sound: keep ~60 ms queued ahead of the picture.
        if (audio) {
            int want = (int)((t + 0.06f) * RATE);
            if (want > chime_frames) want = chime_frames;
            if (want > pushed) { plat_audio_play(chime + pushed * 2, want - pushed); pushed = want; }
        }

        canvas_blit(&c, &bg);
        // Wordmark rising in.
        float a = ease((t - 0.25f) / 0.55f), rise = (1 - a) * 40;
        float x = x0;
        x += draw_text(&c, FONT_DISPLAY, size, x, base + rise, "SAM", C_WHITE, a, track);
        x += draw_text(&c, FONT_DISPLAY, size, x, base + rise, "PSP", C_BLUE, a, track);
        draw_text(&c, FONT_DISPLAY, size, x, base + rise, "4", C_WHITE, a, track);
        // Light bar: grows from the center (blue part + grey tail, as on the app's art).
        float g = ease((t - 0.55f) / 0.6f);
        if (g > 0) {
            float bw = 520 * g, by = base + 70, bx = (SCREEN_W - bw) / 2;
            fill_rrect(&c, bx, by, bw, 6, 3, 0x8A90A0, 0.55f);
            fill_rrect(&c, bx, by, bw * 0.45f, 6, 3, C_BLUE, 1);
            fill_rrect(&c, bx + bw * 0.45f - 22, by - 3, 30, 12, 6, 0xBFD3FF, 0.6f * g);   // bright head
        }
        // What is starting.
        float s = ease((t - 0.95f) / 0.4f);
        if (s > 0) {
            float lw = text_width(FONT_MONO, 22, system_label, 6);
            draw_text(&c, FONT_MONO, 22, (SCREEN_W - lw) / 2, base + 150, system_label, C_BLUE, s, 6);
            if (title && title[0]) {
                char t2[160];
                text_ellipsize(FONT_BODY, 30, title, SCREEN_W - 400, t2, sizeof t2, 0);
                float tw = text_width(FONT_BODY, 30, t2, 0);
                draw_text(&c, FONT_BODY, 30, (SCREEN_W - tw) / 2, base + 200, t2, C_WHITE, s * 0.85f, 0);
            }
        }
        // Fade in from black, out to black.
        float fade = t < 0.25f ? 1 - t / 0.25f : t > DURATION - 0.35f ? (t - (DURATION - 0.35f)) / 0.35f : 0;
        if (fade > 0) fill_rect(&c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, clamp01(fade));
        plat_present(&c);
#ifdef VAULT_HOST
        if (plat_host_done()) break;
#endif
    }
    // Leave the screen black for whoever takes it next.
    memset(c.px, 0, (size_t)SCREEN_W * SCREEN_H * 4);
    plat_present(&c);
    if (audio) plat_audio_close();
    free(chime);
    free(bg.px);
    free(c.px);
}
