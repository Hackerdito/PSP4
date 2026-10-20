// ui.c - the VAULT launcher: header, hero, library carousel, footer and dialogs.
#include "ui.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VAULT_VERSION "v1.7"

// Palette (matches the VAULT mockup)
#define C_BG      0x07080C
#define C_PANEL   0x11131A
#define C_TEXT    0xF2F4F8
#define C_SOFT    0xD5D9E2
#define C_MUTED   0xA3A9B8
#define C_DIM     0x6B7180
#define C_ACCENT  0x3DF5C8
#define C_CSO     0xFF9A5C
#define C_BRAND   0x3F7BFF   // SAMPSP4 blue (the icon's #0042FC, lifted for a dark background)
#define C_INK     0x07080C

#define PAD_X     96
#define TILE_W    288
#define TILE_H    162
#define TILE_GAP  32
#define TILE_Y    716
#define COVER_X   1180
#define COVER_Y   168
#define COVER_W   644
#define COVER_H   362

// Library sections (GameSystem order), switched with L1 / R1.
static const char *kSections[SYS_COUNT] = { "ISO / CSO", "EBOOT y Homebrew", "PS1" };
static const uint32_t kSectionColor[SYS_COUNT] = { 0x3F7BFF, 0xB9A8FF, 0xC9CED6 };
#define C_PBP     0xB9A8FF
#define C_CHD     0xFFD166

// ------------------------------------------------------------------ helpers

static uint32_t fmt_color(DiscFormat f) {
    return f == DISC_CSO ? C_CSO : f == DISC_PBP ? C_PBP : f == DISC_CHD ? C_CHD : (f == DISC_CUE || f == DISC_BIN) ? 0xC9CED6 : C_ACCENT;
}
static const char *fmt_name(DiscFormat f) { return disc_format_name(f); }
static int section_count(App *a, int s) {
    int n = 0;
    for (int k = 0; k < a->lib.count; k++) n += (int)a->lib.games[k].sys == s;
    return n;
}

static int pc_update(App *a, uint32_t pressed);
static void pc_open(App *a);

static Game *selected(App *a) {
    if (a->view_count == 0) return NULL;
    return &a->lib.games[a->view[a->sel]];
}

// PlayStation face-button glyphs, drawn as vectors.
typedef enum { G_CROSS, G_CIRCLE, G_TRIANGLE, G_SQUARE } Glyph;

static void ps_glyph(Canvas *c, Glyph g, float cx, float cy, float r, int filled_bg) {
    static const uint32_t col[4] = { 0x6FA8FF, 0xFF6B6B, 0x3DF5C8, 0xF49BFF };
    uint32_t k = col[g];
    if (filled_bg) fill_circle(c, cx, cy, r, C_INK, 0.85f);
    stroke_circle(c, cx, cy, r - 1, 2, k, 1);
    float s = r * 0.42f;
    switch (g) {
    case G_CROSS:
        draw_line(c, cx - s, cy - s, cx + s, cy + s, 2.2f, k, 1);
        draw_line(c, cx + s, cy - s, cx - s, cy + s, 2.2f, k, 1);
        break;
    case G_CIRCLE: stroke_circle(c, cx, cy, s, 2.2f, k, 1); break;
    case G_TRIANGLE: {
        float h = s * 1.1f;
        draw_line(c, cx, cy - h, cx + h, cy + h * 0.75f, 2.2f, k, 1);
        draw_line(c, cx + h, cy + h * 0.75f, cx - h, cy + h * 0.75f, 2.2f, k, 1);
        draw_line(c, cx - h, cy + h * 0.75f, cx, cy - h, 2.2f, k, 1);
        break;
    }
    case G_SQUARE: stroke_rrect(c, cx - s, cy - s, s * 2, s * 2, 1, 2.2f, k, 1); break;
    }
}

// Little shoulder-button chip ("L1", "R1").
static float key_chip(Canvas *c, float x, float cy, const char *label) {
    float w = text_width(FONT_MONO, 15, label, 0) + 16;
    stroke_rrect(c, x, cy - 14, w, 28, 7, 1.5f, C_MUTED, 0.7f);
    draw_text(c, FONT_MONO, 15, x + 8, cy + 5, label, C_MUTED, 1, 0);
    return w;
}

static float pill(Canvas *c, float x, float y, const char *text, uint32_t bg, float bg_a, uint32_t fg, FontId f, float size, float padx, float h) {
    float w = text_width(f, size, text, 0) + padx * 2;
    fill_rrect(c, x, y, w, h, 8, bg, bg_a);
    draw_text(c, f, size, x + padx, y + h / 2 + font_ascent(f, size) * 0.36f, text, fg, 1, 0);
    return w;
}

// Greedy word wrap into at most `max_lines`; returns line count.
static int wrap_text(FontId f, float size, const char *s, float max_w, char lines[][160], int max_lines) {
    int n = 0;
    char cur[160] = "";
    const char *p = s;
    while (*p && n < max_lines) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        char word[160];
        snprintf(word, sizeof word, "%.*s", (int)(e - p), p);
        char test[320];
        snprintf(test, sizeof test, "%s%s%s", cur, cur[0] ? " " : "", word);
        if (cur[0] && text_width(f, size, test, -1) > max_w) {
            snprintf(lines[n++], 160, "%s", cur);
            snprintf(cur, sizeof cur, "%s", word);
        } else {
            snprintf(cur, sizeof cur, "%s", test);
        }
        p = e;
    }
    if (n < max_lines && cur[0]) snprintf(lines[n++], 160, "%s", cur);
    else if (*p && n == max_lines) {
        // Text left over: ellipsize the last line with the remainder.
        char rest[320];
        snprintf(rest, sizeof rest, "%s %s", lines[n - 1], p);
        text_ellipsize(f, size, rest, max_w, lines[n - 1], 160, -1);
    }
    return n;
}

// Generated art for games without artwork: two-tone shape composition like the mockup.
static void fallback_art(Canvas *c, float x, float y, float w, float h, float r, uint32_t c1, uint32_t c2) {
    fill_rrect(c, x, y, w, h, r, c1, 1);
    // A big disc in the corner, clipped by drawing it inside the card bounds only.
    float R = h * 0.95f, cx = x + w - h * 0.35f, cy = y + h * 0.05f;
    int x0 = (int)x, x1 = (int)(x + w), y0 = (int)y, y1 = (int)(y + h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            float cov = R - sqrtf(dx * dx + dy * dy) + 0.5f;
            if (cov <= 0) continue;
            // keep inside the rounded card
            float qx = fabsf(i + 0.5f - (x + w / 2)) - (w / 2 - r), qy = fabsf(j + 0.5f - (y + h / 2)) - (h / 2 - r);
            float sd = (qx > 0 && qy > 0) ? sqrtf(qx * qx + qy * qy) - r : fmaxf(qx, qy) - r;
            float m = fminf(fminf(cov, 1), fmaxf(0, fminf(1, 0.5f - sd)));
            if (m > 0) fill_rect(c, i, j, 1, 1, c2, m);
        }
    float sy = y + h * 0.66f;
    draw_line(c, x + 2, sy + w * 0.10f, x + w - 2, sy - w * 0.10f, fmaxf(4, h * 0.022f), C_INK, 1);
    draw_line(c, x + 2, sy + w * 0.10f + h * 0.07f, x + w - 2, sy - w * 0.10f + h * 0.07f, fmaxf(2, h * 0.008f), C_INK, 0.6f);
}

// ------------------------------------------------------------------ state

static void rebuild_view(App *a) {
    a->view_count = 0;
    for (int i = 0; i < a->lib.count; i++) {
        if ((int)a->lib.games[i].sys != a->filter) continue;
        a->view[a->view_count++] = i;
    }
    if (a->sel >= a->view_count) a->sel = a->view_count ? a->view_count - 1 : 0;
    a->scroll_target = a->scroll = 0;
    a->bg_for = -2;
}

void app_init(App *a) {
    memset(a, 0, sizeof *a);
    a->bg = canvas_new(SCREEN_W, SCREEN_H);
    a->bg_for = -2;
    a->cover_for = -1;
}

void app_rescan(App *a) {
    image_free(&a->cover);
    a->cover_for = -1;
    library_scan(&a->lib, TILE_W, TILE_H);
    // Stay on the current section if it has games, else open the first one that does.
    if (!section_count(a, a->filter))
        for (int i = 0; i < SYS_COUNT; i++) if (section_count(a, i)) { a->filter = i; break; }
    rebuild_view(a);
    plat_log("scan: %d games in %d folders", a->lib.count, a->lib.folders_found);
}

static void update_scroll_target(App *a) {
    float view_w = SCREEN_W - PAD_X * 2;
    float pos = a->sel * (TILE_W + TILE_GAP);
    if (pos - a->scroll_target > view_w - TILE_W) a->scroll_target = pos - (view_w - TILE_W);
    if (pos < a->scroll_target) a->scroll_target = pos;
}

static void request_launch(App *a, const char *path) {
    snprintf(a->launch_path, sizeof a->launch_path, "%s", path);
    a->launch_request = 1;
}

#define BROWSER_ROWS 10

static void browser_toast(Browser *b, const char *msg) {
    snprintf(b->toast, sizeof b->toast, "%s", msg);
    b->toast_frames = 180;
}

// Input while the Explorar window is open.
static int browser_update(App *a, uint32_t pressed) {
    Browser *b = &a->browser;
    int dirty = 0;
    if (b->toast_frames > 0 && --b->toast_frames == 0) dirty = 1;
    int prev = b->sel;
    if ((pressed & BTN_DOWN) && b->sel < b->count - 1) b->sel++;
    if ((pressed & BTN_UP) && b->sel > 0) b->sel--;
    if (pressed & BTN_R1) b->sel = b->sel + BROWSER_ROWS < b->count ? b->sel + BROWSER_ROWS : (b->count ? b->count - 1 : 0);
    if (pressed & BTN_L1) b->sel = b->sel > BROWSER_ROWS ? b->sel - BROWSER_ROWS : 0;
    if (b->sel != prev) dirty = 1;

    if (pressed & BTN_CROSS && b->count) {
        BrowserEntry *e = &b->entries[b->sel];
        if (e->kind == ENTRY_DRIVE && strcmp(e->full, "pc:") == 0) pc_open(a);
        else if (e->kind == ENTRY_DRIVE || e->kind == ENTRY_DIR) browser_enter(b, e->full);
        else if (e->kind == ENTRY_GAME) request_launch(a, e->full);
        else browser_toast(b, "SAMPSP4 abre juegos .iso, .cso, EBOOT.PBP y .chd");
        dirty = 1;
    }
    if (pressed & BTN_TRIANGLE) {
        const char *folder = NULL;
        if (!b->at_root) folder = b->path;
        else if (b->count && strcmp(b->entries[b->sel].full, "pc:") != 0) folder = b->entries[b->sel].full;
        if (folder) {
            int r = library_folder_add(folder);
            char msg[160];
            char fp[300];
            browser_display_path(folder, fp, sizeof fp);
            if (r > 0) { snprintf(msg, sizeof msg, "Agregada a tu biblioteca: %s", fp); b->folders_added = 1; }
            else if (r == 0) snprintf(msg, sizeof msg, "Esa carpeta ya est\xC3\xA1 en tu biblioteca");
            else snprintf(msg, sizeof msg, "No se pudo guardar la carpeta");
            browser_toast(b, msg);
        }
        dirty = 1;
    }
    if (pressed & BTN_CIRCLE) {
        browser_up(b);
        if (!b->open && b->folders_added) a->scanning = 1;
        dirty = 1;
    }
    // Keep the selection inside the visible rows, scrolling smoothly.
    // (Measured from the row the list is heading to, not from the animated position: comparing with
    // a half-way scroll value stopped the list one row short and the selection fell off the bottom.)
    if (b->sel < b->top) b->top = b->sel;
    if (b->sel >= b->top + BROWSER_ROWS) b->top = b->sel - BROWSER_ROWS + 1;
    float target = (float)b->top;
    float d = target - b->scroll;
    if (fabsf(d) > 0.02f) { b->scroll += d * 0.35f; dirty = 1; }
    else if (d != 0) { b->scroll = target; dirty = 1; }
    return dirty;
}

int app_update(App *a, uint32_t pressed, uint32_t held) {
    int dirty = 0;
    (void)held;
    if (a->modal != MODAL_NONE) {
        if (pressed & (BTN_CIRCLE | BTN_CROSS)) { a->modal = MODAL_NONE; dirty = 1; }
        return dirty;
    }
    if (a->pc.open) return pc_update(a, pressed);
    if (a->browser.open) return browser_update(a, pressed);
    int prev_sel = a->sel;
    if ((pressed & BTN_RIGHT) && a->sel < a->view_count - 1) a->sel++;
    if ((pressed & BTN_LEFT) && a->sel > 0) a->sel--;
    if (pressed & (BTN_R1 | BTN_L1)) {
        a->filter = (a->filter + ((pressed & BTN_R1) ? 1 : SYS_COUNT - 1)) % SYS_COUNT;
        a->sel = 0;
        rebuild_view(a);
        dirty = 1;
    }
    if (pressed & BTN_SQUARE) { browser_open(&a->browser); dirty = 1; }
    if (pressed & BTN_L2) { pc_open(a); dirty = 1; }
    if (pressed & BTN_OPTIONS) { a->scanning = 1; dirty = 1; }
    if ((pressed & BTN_CROSS) && a->view_count) { request_launch(a, a->lib.games[a->view[a->sel]].path); dirty = 1; }
    if ((pressed & BTN_TRIANGLE) && a->view_count) { a->modal = MODAL_DETAILS; dirty = 1; }
    if ((pressed & BTN_CROSS) && !a->view_count) { browser_open(&a->browser); dirty = 1; }

    if (a->sel != prev_sel) { dirty = 1; a->idle_frames = 0; }
    else a->idle_frames++;
    update_scroll_target(a);

    // Smooth carousel scroll.
    float d = a->scroll_target - a->scroll;
    if (fabsf(d) > 0.5f) { a->scroll += d * 0.28f; dirty = 1; }
    else if (d != 0) { a->scroll = a->scroll_target; dirty = 1; }

    // Load the big cover once the selection settles (keeps scrolling snappy on slow USB drives).
    Game *g = selected(a);
    int gi = g ? a->view[a->sel] : -1;
    if (gi != a->cover_for && a->idle_frames >= 8) {
        image_free(&a->cover);
        if (g) a->cover = library_load_cover(g, COVER_W, COVER_H);
        a->cover_for = gi;
        dirty = 1;
    }
    return dirty;
}

// ------------------------------------------------------------------ drawing

static void draw_header(App *a, Canvas *c, int hh, int mm) {
    float y = 84;
    // Wordmark: SAM PSP 4, with PSP in the brand blue (as on the app icon).
    float wx = PAD_X;
    wx += draw_text(c, FONT_DISPLAY, 32, wx, y - 6, "SAM", C_TEXT, 1, 3);
    wx += draw_text(c, FONT_DISPLAY, 32, wx, y - 6, "PSP", C_BRAND, 1, 3);
    draw_text(c, FONT_DISPLAY, 32, wx, y - 6, "4", C_TEXT, 1, 3);

    // Sections (L1 / R1): a dot in the section's color, its name and how many games it has.
    float x = PAD_X + 260;
    x += key_chip(c, x, y - 17, "L1") + 14;
    for (int i = 0; i < SYS_COUNT; i++) {
        char n[8];
        snprintf(n, sizeof n, "%d", section_count(a, i));
        int on = a->filter == i;
        float tw = text_width(FONT_DISPLAY, 22, kSections[i], 0), nw = text_width(FONT_MONO, 18, n, 0);
        float w = 30 + 14 + tw + 12 + nw + 26;
        fill_rrect(c, x, y - 42, w, 50, 25, on ? C_TEXT : 0xFFFFFF, on ? 1 : 0.06f);
        fill_rrect(c, x + 22, y - 23, 12, 12, 6, kSectionColor[i], 1);
        draw_text(c, FONT_DISPLAY, 22, x + 44, y - 9, kSections[i], on ? C_INK : C_SOFT, 1, 0);
        draw_text(c, FONT_MONO, 18, x + 44 + tw + 12, y - 10, n, on ? 0x5A6070 : C_MUTED, 1, 0);
        x += w + 10;
    }
    key_chip(c, x + 4, y - 17, "R1");

    // Storage summary + clock on the right.
    char clock[8], info[96], sz[24];
    snprintf(clock, sizeof clock, "%02d:%02d", hh, mm);
    format_size(a->lib.total_bytes, sz, sizeof sz);
    snprintf(info, sizeof info, "%d %s \xC2\xB7 %s", a->lib.count, a->lib.count == 1 ? "juego" : "juegos", sz);
    float cw = text_width(FONT_MONO, 24, clock, 0);
    draw_text(c, FONT_MONO, 24, SCREEN_W - PAD_X - cw, y - 9, clock, C_TEXT, 1, 0);
    float iw = text_width(FONT_MONO, 18, info, 0);
    draw_text(c, FONT_MONO, 18, SCREEN_W - PAD_X - cw - 40 - iw, y - 11, info, C_MUTED, 1, 0);
}

static float draw_button(Canvas *c, float x, float y, const char *label, Glyph g, int primary) {
    float h = 72, tw = text_width(FONT_DISPLAY, 26, label, 0);
    float w = tw + 44 + 40 + 20;
    if (primary) fill_rrect(c, x, y, w, h, 16, C_ACCENT, 1);
    else { fill_rrect(c, x, y, w, h, 16, 0xFFFFFF, 0.08f); stroke_rrect(c, x, y, w, h, 16, 1.5f, 0xFFFFFF, 0.2f); }
    if (primary) {
        // Ink-coloured glyph on the accent button.
        float cx = x + 40, cy = y + h / 2, s = 8;
        stroke_circle(c, cx, cy, 17, 2.4f, C_INK, 1);
        if (g == G_CROSS) { draw_line(c, cx - s, cy - s, cx + s, cy + s, 2.6f, C_INK, 1); draw_line(c, cx + s, cy - s, cx - s, cy + s, 2.6f, C_INK, 1); }
        else if (g == G_SQUARE) stroke_rrect(c, cx - s, cy - s, s * 2, s * 2, 1, 2.6f, C_INK, 1);
    } else {
        ps_glyph(c, g, x + 40, y + h / 2, 18, 0);
    }
    draw_text(c, FONT_DISPLAY, 26, x + 72, y + h / 2 + 9, label, primary ? C_INK : C_TEXT, 1, 0);
    return w;
}

static void draw_cover(App *a, Canvas *c, Game *g) {
    shadow_rrect(c, COVER_X, COVER_Y + 26, COVER_W, COVER_H, 28, 70, 0.55f);
    int gi = a->view[a->sel];
    if (a->cover.px && a->cover_for == gi) {
        fill_rrect(c, COVER_X, COVER_Y, COVER_W, COVER_H, 28, g->c1, 1);
        draw_image_rounded(c, &a->cover, COVER_X, COVER_Y, 28, 1);
    } else if (g->tile.px && a->cover_for != gi) {
        // Still settling: show the tile art stretched softly as a placeholder.
        fallback_art(c, COVER_X, COVER_Y, COVER_W, COVER_H, 28, mix(g->c1, C_BG, 0.35f), mix(g->c2, C_BG, 0.35f));
    } else {
        fallback_art(c, COVER_X, COVER_Y, COVER_W, COVER_H, 28, g->c1, g->c2);
        char t[160];
        text_ellipsize(FONT_DISPLAY, 44, g->title, COVER_W - 64, t, sizeof t, -0.5f);
        draw_text(c, FONT_DISPLAY, 44, COVER_X + 32, COVER_Y + COVER_H - 32, t, C_INK, 1, -0.5f);
    }
    // File name tag on the cover.
    char tag[96];
    text_ellipsize(FONT_MONO, 15, g->file, COVER_W - 120, tag, sizeof tag, 0);
    float tw = text_width(FONT_MONO, 15, tag, 0);
    fill_rrect(c, COVER_X + 24, COVER_Y + 22, tw + 24, 30, 8, C_INK, 0.72f);
    draw_text(c, FONT_MONO, 15, COVER_X + 36, COVER_Y + 42, tag, C_SOFT, 1, 0);
}

// When nothing was found: what the scan saw, folder by folder, so the reason is on screen.
static void draw_empty_report(App *a, Canvas *c, float x, float y) {
    Library *L = &a->lib;
    if (!L->data_readable) {
        draw_text(c, FONT_BODY, 26, x, y, "SAMPSP4 no pudo leer /data: sigue dentro del sandbox.", 0xFF8A80, 1, 0);
        draw_text(c, FONT_BODY, 22, x, y + 36, "Activa GoldHEN antes de abrir SAMPSP4 y vuelve a entrar.", C_SOFT, 1, 0);
        y += 84;
    } else {
        draw_text(c, FONT_BODY, 24, x, y, "Esto encontr\xC3\xA9 (.iso .cso .pbp .chd; las USB se revisan completas):", C_SOFT, 1, 0);
        y += 46;
    }
    for (int i = 0; i < L->report_count && i < 6; i++) {
        const DirReport *r = &L->reports[i];
        char status[200];
        uint32_t col = C_MUTED;
        if (!r->exists) snprintf(status, sizeof status, "no existe");
        else if (r->games) { snprintf(status, sizeof status, "%d juego%s", r->games, r->games == 1 ? "" : "s"); col = C_ACCENT; }
        else if (!r->files && !r->subdirs) snprintf(status, sizeof status, "vac\xC3\xAD" "a");
        else if (!r->files) snprintf(status, sizeof status, "%d carpeta%s sin juegos", r->subdirs, r->subdirs == 1 ? "" : "s");
        else {
            int packed = strstr(r->other, ".zip") || strstr(r->other, ".rar") || strstr(r->other, ".7z");
            snprintf(status, sizeof status, "%s%s", r->other, packed ? "  \xE2\x80\x94 descompr\xC3\xAD" "melos" : "");
            col = 0xFFB86B;
        }
        char path[160];
        if (r->exists && strcmp(r->used, r->path) != 0) snprintf(path, sizeof path, "%s", r->used);
        else snprintf(path, sizeof path, "%s", r->path);
        char pe[160];
        text_ellipsize(FONT_MONO, 19, path, 470, pe, sizeof pe, 0);
        draw_text(c, FONT_MONO, 19, x, y, pe, r->exists ? C_TEXT : C_DIM, 1, 0);
        char se[200];
        text_ellipsize(FONT_BODY, 20, status, 470, se, sizeof se, 0);
        draw_text(c, FONT_BODY, 20, x + 500, y, se, col, 1, 0);
        y += 34;
    }
    draw_text(c, FONT_MONO, 16, x, y + 12, "Registro completo: /data/vault/scan.log", C_DIM, 1, 0);
}

static void draw_hero(App *a, Canvas *c) {
    Game *g = selected(a);
    float x = PAD_X, y = 236;
    if (!g) {
        draw_text(c, FONT_MONO, 18, x, y, a->lib.count ? "SECCI\xC3\x93N VAC\xC3\x8D" "A" : "BIENVENIDO A SAMPSP4", C_ACCENT, 1, 3.6f);
        draw_text(c, FONT_DISPLAY, 76, x - 4, y + 88, a->lib.count ? "Nada por aqu\xC3\xAD" : "Tu biblioteca est\xC3\xA1 vac\xC3\xAD" "a", C_TEXT, 1, -2);
        if (a->lib.count) {
            static const char *kHint[SYS_COUNT] = {
                "Aqu\xC3\xAD van tus juegos de PSP en .iso, .cso o .chd.",
                "Aqu\xC3\xAD van los EBOOT.PBP: juegos de PSN y homebrew de PSP.",
                "Aqu\xC3\xAD van tus juegos de PS1: .cue/.bin, .chd, .iso o EBOOT.PBP.",
            };
            draw_text(c, FONT_BODY, 26, x, y + 170, kHint[a->filter], C_SOFT, 1, 0);
            draw_text(c, FONT_BODY, 26, x, y + 210, "Cambia de secci\xC3\xB3n con L1 / R1.", C_MUTED, 1, 0);
        } else {
            draw_empty_report(a, c, x, y + 146);
        }
        fallback_art(c, COVER_X, COVER_Y, COVER_W, COVER_H, 28, 0x1B2230, 0x24304A);
        draw_text(c, FONT_DISPLAY, 44, COVER_X + 32, COVER_Y + COVER_H - 32, kSections[a->filter], C_TEXT, 0.9f, -0.5f);
        return;
    }

    char label[96];
    const char *sys = g->is_ps1 ? "PS1" : g->is_psp ? "PSP" : "DISCO";
    if (g->disc_id[0]) snprintf(label, sizeof label, "%s \xC2\xB7 %s \xC2\xB7 %s", sys, g->disc_id, g->source);
    else snprintf(label, sizeof label, "%s \xC2\xB7 %s", sys, g->source);
    for (char *p = label; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    draw_text(c, FONT_MONO, 18, x, y, label, C_ACCENT, 1, 3.6f);

    // Title: up to two lines at 92px; long titles drop to 72px.
    char lines[2][160];
    float size = 92;
    int n = wrap_text(FONT_DISPLAY, size, g->title, 1000, lines, 2);
    if (n == 2 && text_width(FONT_DISPLAY, size, lines[1], -2.5f) > 1000) { size = 72; n = wrap_text(FONT_DISPLAY, size, g->title, 1000, lines, 2); }
    float ty = y + size * 1.08f;
    for (int i = 0; i < n; i++) { draw_text(c, FONT_DISPLAY, size, x - 4, ty, lines[i], C_TEXT, 1, -2.5f); ty += size * 0.98f; }

    // Badges.
    float by = ty - size * 0.98f + 40, bx = x;
    char sz[24], ratio[32];
    bx += pill(c, bx, by, fmt_name(g->fmt), fmt_color(g->fmt), 1, C_INK, FONT_MONO, 18, 16, 40) + 12;
    bx += pill(c, bx, by, g->is_ps1 ? "PlayStation" : g->is_psp ? "PSP" : "Desconocido", 0xFFFFFF, 0.08f, C_SOFT, FONT_MONO, 18, 16, 40) + 12;
    format_size(g->image_size, sz, sizeof sz);
    bx += pill(c, bx, by, sz, 0xFFFFFF, 0.08f, C_SOFT, FONT_MONO, 18, 16, 40) + 12;
    if (g->fmt == DISC_CSO && g->image_size) {
        snprintf(ratio, sizeof ratio, "%d%% comprimido", (int)(100 - g->file_size * 100 / g->image_size));
        pill(c, bx, by, ratio, 0xFFFFFF, 0.08f, C_SOFT, FONT_MONO, 18, 16, 40);
    }

    float btn_y = by + 72;
    float bw = draw_button(c, x, btn_y, "Jugar", G_CROSS, 1);
    bw += 16 + draw_button(c, x + bw + 16, btn_y, "Detalles", G_TRIANGLE, 0);
    draw_button(c, x + bw + 16, btn_y, "Explorar", G_SQUARE, 0);

    draw_cover(a, c, g);
}

static void draw_tile(Canvas *c, Game *g, float x, float y, int on) {
    if (on) {
        shadow_rrect(c, x, y + 18, TILE_W, TILE_H, 18, 44, 0.6f);
        stroke_rrect(c, x - 10, y - 10, TILE_W + 20, TILE_H + 20, 26, 6, C_ACCENT, 0.33f);
    }
    if (g->tile.px) {
        fill_rrect(c, x, y, TILE_W, TILE_H, 18, mix(g->c1, C_BG, 0.6f), 1);
        draw_image_rounded(c, &g->tile, (int)x, (int)y, 18, on ? 1 : 0.82f);
    } else {
        fallback_art(c, x, y, TILE_W, TILE_H, 18, g->c1, g->c2);
        char t[160];
        text_ellipsize(FONT_DISPLAY, 24, g->title, TILE_W - 32, t, sizeof t, 0);
        draw_text(c, FONT_DISPLAY, 24, x + 16, y + TILE_H - 18, t, C_INK, 1, 0);
        if (!on) fill_rrect(c, x, y, TILE_W, TILE_H, 18, C_BG, 0.18f);
    }
    if (on) stroke_rrect(c, x - 2, y - 2, TILE_W + 4, TILE_H + 4, 20, 4, C_TEXT, 1);
    pill(c, x + 12, y + 12, fmt_name(g->fmt), C_INK, 0.85f, fmt_color(g->fmt), FONT_MONO, 14, 9, 26);

    char t[160];
    text_ellipsize(FONT_BODY, 21, g->title, TILE_W, t, sizeof t, 0);
    draw_text(c, FONT_BODY, 21, x + 2, y + TILE_H + 40, t, on ? C_TEXT : C_MUTED, 1, 0);
}

static void draw_library(App *a, Canvas *c) {
    float y = 668;
    draw_text(c, FONT_DISPLAY, 32, PAD_X, y, "Tu biblioteca", C_TEXT, 1, 0);
    char sub[96];
    snprintf(sub, sizeof sub, "%d de %d \xC2\xB7 %s", a->view_count ? a->sel + 1 : 0, a->view_count, kSections[a->filter]);
    draw_text(c, FONT_MONO, 18, PAD_X + 248, y - 2, sub, C_MUTED, 1, 0);

    if (!a->view_count) {
        float x = PAD_X;
        for (int i = 0; i < 4; i++) {
            float tx = x + i * (TILE_W + TILE_GAP);
            stroke_rrect(c, tx, TILE_Y, TILE_W, TILE_H, 18, 2, 0xFFFFFF, i == 0 ? 0.28f : 0.1f);
        }
        draw_text(c, FONT_BODY, 22, x + 44, TILE_Y + TILE_H / 2 + 8, "Sin juegos todav\xC3\xAD" "a", C_MUTED, 1, 0);
        return;
    }
    for (int i = 0; i < a->view_count; i++) {
        float x = PAD_X + i * (TILE_W + TILE_GAP) - a->scroll;
        if (x > SCREEN_W || x + TILE_W < 0) continue;
        int on = i == a->sel;
        draw_tile(c, &a->lib.games[a->view[i]], x, on ? TILE_Y - 10 : TILE_Y, on);
    }
    // Scroll hints.
    if (a->scroll > 1) fill_triangle(c, 48, TILE_Y + 70, 64, TILE_Y + 58, 64, TILE_Y + 82, C_TEXT, 0.6f);
    float last = PAD_X + (a->view_count - 1) * (TILE_W + TILE_GAP) - a->scroll + TILE_W;
    if (last > SCREEN_W - 40) fill_triangle(c, SCREEN_W - 48, TILE_Y + 70, SCREEN_W - 64, TILE_Y + 58, SCREEN_W - 64, TILE_Y + 82, C_TEXT, 0.6f);
}

static void draw_footer(App *a, Canvas *c) {
    float y = 1000;
    fill_rect(c, 0, (int)y, SCREEN_W, SCREEN_H - (int)y, C_BG, 0.72f);
    fill_rect(c, 0, (int)y, SCREEN_W, 1, 0xFFFFFF, 0.08f);
    float cy = y + 40;
    float lw = draw_text(c, FONT_MONO, 18, PAD_X, cy + 6, "SAMPSP4 " VAULT_VERSION " \xC2\xB7 PSP + PS1", C_MUTED, 1, 0);
    if (!a->browser.open && !a->pc.open && a->modal == MODAL_NONE) {
        float kx = PAD_X + lw + 40;
        kx += key_chip(c, kx, cy, "OPTIONS") + 10;
        kx += draw_text(c, FONT_BODY, 20, kx, cy + 7, "Escanear", C_MUTED, 1, 0) + 32;
        kx += key_chip(c, kx, cy, "L2") + 10;
        draw_text(c, FONT_BODY, 20, kx, cy + 7, "Mi PC", C_MUTED, 1, 0);
    }

    struct { Glyph g; const char *t; } hints[4] = {
        { G_CROSS, "Jugar" }, { G_TRIANGLE, "Detalles" }, { G_SQUARE, "Explorar" },
    };
    int nh = 3;
    if (!a->view_count) { hints[0].t = "Explorar"; nh = 1; }
    if (a->browser.open) {
        int game = a->browser.count && a->browser.entries[a->browser.sel].kind == ENTRY_GAME;
        hints[0].g = G_CROSS; hints[0].t = game ? "Jugar" : "Abrir";
        hints[1].g = G_TRIANGLE; hints[1].t = "Agregar carpeta";
        hints[2].g = G_CIRCLE; hints[2].t = a->browser.at_root ? "Cerrar" : "Atr\xC3\xA1s";
        nh = 3;
    }
    if (a->pc.open) {
        PcView *p = &a->pc;
        nh = 0;
        if (p->kb_open) nh = 0;
        else if (p->busy && p->screen != PCV_DOWNLOAD) { hints[0].g = G_CIRCLE; hints[0].t = "Dejar de esperar"; nh = 1; }
        else switch (p->screen) {
        case PCV_SETUP: hints[0].g = G_CROSS; hints[0].t = p->field <= 2 ? "Escribir" : "Elegir"; hints[1].g = G_CIRCLE; hints[1].t = "Cerrar"; nh = 2; break;
        case PCV_SHARES: hints[0].g = G_CROSS; hints[0].t = "Abrir"; hints[1].g = G_CIRCLE; hints[1].t = "Cerrar"; nh = 2; break;
        case PCV_DIR: {
            int game = p->count && p->entries[p->sel].kind == PC_KIND_GAME;
            hints[0].g = G_CROSS; hints[0].t = game ? "Descargar" : "Abrir";
            hints[1].g = G_TRIANGLE; hints[1].t = "Ajustes";
            hints[2].g = G_CIRCLE; hints[2].t = p->dir[0] ? "Atr\xC3\xA1s" : "Cerrar";
            nh = 3;
            break;
        }
        case PCV_DEST: hints[0].g = G_CROSS; hints[0].t = "Descargar aqu\xC3\xAD"; hints[1].g = G_CIRCLE; hints[1].t = "Atr\xC3\xA1s"; nh = 2; break;
        case PCV_DOWNLOAD: hints[0].g = G_CIRCLE; hints[0].t = "Cancelar"; nh = 1; break;
        case PCV_RESULT: hints[0].g = G_CROSS; hints[0].t = "Seguir"; nh = 1; break;
        }
    }
    if (a->modal != MODAL_NONE) { hints[0].g = G_CIRCLE; hints[0].t = "Cerrar"; nh = 1; }
    float x = SCREEN_W - PAD_X;
    for (int i = nh - 1; i >= 0; i--) {
        float tw = text_width(FONT_BODY, 20, hints[i].t, 0);
        x -= tw;
        draw_text(c, FONT_BODY, 20, x, cy + 7, hints[i].t, C_MUTED, 1, 0);
        x -= 24;
        ps_glyph(c, hints[i].g, x, cy, 13, 0);
        x -= 13 + 40;
    }
}

static void draw_modal(App *a, Canvas *c) {
    Game *g = selected(a);
    if (!g && a->modal == MODAL_DETAILS) return;
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, 0.62f);
    float w = 1000, h = a->modal == MODAL_DETAILS ? 560 : 440, x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 20;
    shadow_rrect(c, x, y + 30, w, h, 28, 80, 0.7f);
    fill_rrect(c, x, y, w, h, 28, C_PANEL, 1);
    stroke_rrect(c, x, y, w, h, 28, 1.5f, 0xFFFFFF, 0.1f);
    float px = x + 56, py = y + 84;

    if (a->modal == MODAL_NO_ENGINE || a->modal == MODAL_LAUNCH_ERROR || a->modal == MODAL_PS1) {
        char l1[160], l2[160], code[96];
        const char *title;
        if (a->modal == MODAL_PS1) {
            title = "Juego de PS1";
            snprintf(l1, sizeof l1, "Este EBOOT.PBP es un cl\xC3\xA1sico de PlayStation 1, no de PSP.");
            snprintf(l2, sizeof l2, "SAMPSP4 todav\xC3\xAD" "a no trae motor de PS1: llegar\xC3\xA1 en una pr\xC3\xB3xima versi\xC3\xB3n.");
            snprintf(code, sizeof code, "%s", g ? g->file : "EBOOT.PBP");
        } else if (a->modal == MODAL_NO_ENGINE) {
            title = "Falta el motor PSP";
            snprintf(l1, sizeof l1, "VAULT juega con M33 (PPSSPP para PS4). Inst\xC3\xA1lalo una vez:");
            snprintf(l2, sizeof l2, "descarga M33 v1.03 o m\xC3\xA1s nuevo e inst\xC3\xA1lalo con el Package Installer.");
            snprintf(code, sizeof code, "github.com/iHaiDeeZ/M33/releases \xC2\xB7 c\xC3\xB3" "digo 0x%08X", (unsigned)a->launch_error);
        } else {
            title = "No se pudo abrir M33";
            snprintf(l1, sizeof l1, "La consola rechaz\xC3\xB3 el lanzamiento. Abre M33 desde el men\xC3\xBA:");
            snprintf(l2, sizeof l2, "si tu juego est\xC3\xA1 en /data/March33/games aparece en su lista.");
            snprintf(code, sizeof code, "C\xC3\xB3" "digo 0x%08X \xC2\xB7 %s", (unsigned)a->launch_error, ENGINE_TITLE_ID);
        }
        draw_text(c, FONT_MONO, 18, px, py - 20, a->modal == MODAL_PS1 ? "FORMATO" : "MOTOR PSP", C_ACCENT, 1, 3.6f);
        draw_text(c, FONT_DISPLAY, 52, px - 2, py + 44, title, C_TEXT, 1, -1);
        draw_text(c, FONT_BODY, 24, px, py + 104, l1, C_SOFT, 1, 0);
        draw_text(c, FONT_BODY, 24, px, py + 140, l2, C_SOFT, 1, 0);
        fill_rrect(c, px, py + 176, w - 112, 52, 12, 0xFFFFFF, 0.05f);
        draw_text(c, FONT_MONO, 18, px + 18, py + 208, code, C_MUTED, 1, 0);
        float bx = px, by = y + h - 104;
        fill_rrect(c, bx, by, 250, 64, 16, 0xFFFFFF, 0.1f);
        ps_glyph(c, G_CIRCLE, bx + 36, by + 32, 16, 0);
        draw_text(c, FONT_DISPLAY, 24, bx + 66, by + 40, "Entendido", C_TEXT, 1, 0);
        return;
    }

    draw_text(c, FONT_MONO, 18, px, py - 20, "DETALLES", C_ACCENT, 1, 3.6f);
    char t[160];
    text_ellipsize(FONT_DISPLAY, 48, g->title, w - 112, t, sizeof t, -1);
    draw_text(c, FONT_DISPLAY, 48, px - 2, py + 40, t, C_TEXT, 1, -1);
    char disk[24], real[24], comp[32], path[160];
    format_size(g->file_size, disk, sizeof disk);
    format_size(g->image_size, real, sizeof real);
    if (g->fmt == DISC_CSO && g->image_size) snprintf(comp, sizeof comp, "%d%% ahorrado", (int)(100 - g->file_size * 100 / g->image_size));
    else if (g->fmt == DISC_CHD) snprintf(comp, sizeof comp, "Comprimido (CHD)");
    else if (g->fmt == DISC_PBP) snprintf(comp, sizeof comp, "Paquete EBOOT");
    else snprintf(comp, sizeof comp, "Sin compresi\xC3\xB3n");
    char friendly[512];
    browser_display_path(g->path, friendly, sizeof friendly);
    text_ellipsize(FONT_MONO, 20, friendly, w - 112 - 220, path, sizeof path, 0);
    const char *rows[][2] = {
        { "Archivo", path },
        { "Formato", g->fmt == DISC_CSO ? "CSO (ISO comprimido)" : g->fmt == DISC_PBP ? (g->is_ps1 ? "EBOOT.PBP (PSone Classic)" : "EBOOT.PBP (PSN / homebrew)")
                    : g->fmt == DISC_CHD ? "CHD (comprimido)" : g->fmt == DISC_CUE ? "BIN / CUE" : g->fmt == DISC_BIN ? "BIN" : "ISO" },
        { "ID de disco", g->disc_id[0] ? g->disc_id : "\xE2\x80\x94" },
        { "En disco", disk },
        { "Tama\xC3\xB1o real", real },
        { "Compresi\xC3\xB3n", comp },
        { "Origen", g->source },
    };
    float ry = py + 100;
    for (int i = 0; i < 7; i++) {
        draw_text(c, FONT_BODY, 20, px, ry, rows[i][0], C_MUTED, 1, 0);
        draw_text(c, FONT_MONO, 20, px + 220, ry, rows[i][1], C_TEXT, 1, 0);
        ry += 44;
    }
}


// ------------------------------------------------------------------ Explorar window

static void icon_drive(Canvas *c, float x, float y, uint32_t col) {
    stroke_rrect(c, x, y + 6, 34, 22, 5, 2, col, 1);
    fill_circle(c, x + 26, y + 17, 2.5f, col, 1);
    fill_rect(c, (int)x + 7, (int)y + 15, 12, 2, col, 0.8f);
}
static void icon_folder(Canvas *c, float x, float y, uint32_t col) {
    fill_rrect(c, x, y + 4, 14, 8, 2, col, 0.9f);
    fill_rrect(c, x, y + 8, 34, 22, 4, col, 0.9f);
}
static void icon_disc(Canvas *c, float x, float y, uint32_t col) {
    fill_circle(c, x + 17, y + 17, 15, col, 1);
    fill_circle(c, x + 17, y + 17, 4, C_PANEL, 1);
    stroke_circle(c, x + 17, y + 17, 9, 1.2f, C_INK, 0.35f);
}
static void icon_file(Canvas *c, float x, float y, uint32_t col) {
    stroke_rrect(c, x + 6, y + 2, 22, 30, 3, 1.8f, col, 1);
    fill_rect(c, (int)x + 11, (int)y + 12, 12, 2, col, 0.8f);
    fill_rect(c, (int)x + 11, (int)y + 18, 12, 2, col, 0.8f);
}

static void draw_browser(App *a, Canvas *c) {
    Browser *b = &a->browser;
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, 0.66f);
    float w = 1600, h = 872, x = (SCREEN_W - w) / 2, y = 88;
    shadow_rrect(c, x, y + 30, w, h, 32, 90, 0.7f);
    fill_rrect(c, x, y, w, h, 32, C_PANEL, 1);
    stroke_rrect(c, x, y, w, h, 32, 1.5f, 0xFFFFFF, 0.1f);
    float px = x + 56;

    draw_text(c, FONT_MONO, 18, px, y + 62, "EXPLORAR", C_ACCENT, 1, 3.6f);
    char where[512], we[300];
    if (b->at_root) snprintf(where, sizeof where, "Unidades");
    else browser_display_path(b->path, where, sizeof where);
    text_ellipsize(FONT_DISPLAY, 38, where, w - 460, we, sizeof we, -0.5f);
    draw_text(c, FONT_DISPLAY, 38, px - 2, y + 112, we, C_TEXT, 1, -0.5f);
    int games = 0, dirs = 0;
    for (int i = 0; i < b->count; i++) { games += b->entries[i].kind == ENTRY_GAME; dirs += b->entries[i].kind == ENTRY_DIR; }
    char count[96];
    if (b->at_root) snprintf(count, sizeof count, "%d unidad%s", b->count, b->count == 1 ? "" : "es");
    else snprintf(count, sizeof count, "%d juego%s \xC2\xB7 %d carpeta%s%s", games, games == 1 ? "" : "s", dirs, dirs == 1 ? "" : "s", b->truncated ? " \xC2\xB7 +" : "");
    float cw = text_width(FONT_MONO, 18, count, 0);
    draw_text(c, FONT_MONO, 18, x + w - 56 - cw, y + 104, count, C_MUTED, 1, 0);
    fill_rect(c, (int)px, (int)(y + 140), (int)(w - 112), 1, 0xFFFFFF, 0.08f);

    const float row_h = 62, list_y = y + 156, list_w = w - 112;
    if (b->error[0]) {
        draw_text(c, FONT_BODY, 22, px, list_y + 40, b->error, 0xFF8A80, 1, 0);
    }
    if (!b->count) {
        const char *empty = b->at_root ? "No encontr\xC3\xA9 ninguna unidad. Conecta una memoria USB." : "Carpeta vac\xC3\xAD" "a";
        float ew = text_width(FONT_BODY, 26, empty, 0);
        draw_text(c, FONT_BODY, 26, x + (w - ew) / 2, list_y + 5 * row_h, empty, C_MUTED, 1, 0);
    }
    for (int i = 0; i < b->count; i++) {
        float rel = i - b->scroll;
        if (rel < -0.5f || rel > BROWSER_ROWS - 0.5f) continue;
        float ry = list_y + rel * row_h;
        BrowserEntry *e = &b->entries[i];
        int on = i == b->sel;
        if (on) {
            fill_rrect(c, px - 12, ry, list_w + 24, row_h - 6, 14, C_ACCENT, 0.12f);
            stroke_rrect(c, px - 12, ry, list_w + 24, row_h - 6, 14, 2, C_ACCENT, 0.7f);
        }
        float iy = ry + (row_h - 6) / 2 - 17;
        uint32_t ic = e->kind == ENTRY_GAME ? C_ACCENT : e->kind == ENTRY_FILE ? C_DIM : C_SOFT;
        switch (e->kind) {
        case ENTRY_DRIVE: icon_drive(c, px + 4, iy, ic); break;
        case ENTRY_DIR: icon_folder(c, px + 4, iy, ic); break;
        case ENTRY_GAME: icon_disc(c, px + 4, iy, fmt_color((DiscFormat)disc_format_from_name(e->name))); break;
        default: icon_file(c, px + 4, iy, ic); break;
        }
        char name[200];
        float note_w = text_width(FONT_MONO, 18, e->note, 0);
        text_ellipsize(e->kind == ENTRY_FILE ? FONT_BODY : FONT_DISPLAY, 24, e->name, list_w - 120 - note_w - 120, name, sizeof name, 0);
        float base = ry + (row_h - 6) / 2 + 8;
        uint32_t tc = e->kind == ENTRY_FILE ? C_DIM : C_TEXT;
        float nw = draw_text(c, e->kind == ENTRY_FILE ? FONT_BODY : FONT_DISPLAY, 24, px + 60, base, name, tc, 1, 0);
        if (e->kind == ENTRY_GAME) {
            DiscFormat ef = (DiscFormat)disc_format_from_name(e->name);
            pill(c, px + 60 + nw + 16, ry + (row_h - 6) / 2 - 13, fmt_name(ef), fmt_color(ef), 1, C_INK, FONT_MONO, 14, 9, 26);
        }
        draw_text(c, FONT_MONO, 18, px + list_w - note_w, base - 1, e->note, C_MUTED, 1, 0);
    }
    // Scrollbar
    if (b->count > BROWSER_ROWS) {
        float track_h = BROWSER_ROWS * row_h - 6, th = fmaxf(40, track_h * BROWSER_ROWS / b->count);
        float ty = list_y + (track_h - th) * (b->scroll / (b->count - BROWSER_ROWS));
        fill_rrect(c, x + w - 26, list_y, 6, track_h, 3, 0xFFFFFF, 0.06f);
        fill_rrect(c, x + w - 26, ty, 6, th, 3, 0xFFFFFF, 0.35f);
    }
    // Bottom line: feedback, or what Triangle does here.
    float by = y + h - 40;
    fill_rect(c, (int)px, (int)(by - 44), (int)(w - 112), 1, 0xFFFFFF, 0.08f);
    if (b->toast_frames > 0 && b->toast[0]) {
        draw_text(c, FONT_BODY, 22, px, by, b->toast, C_ACCENT, 1, 0);
    } else {
        const char *tip = b->at_root ? "Agrega la unidad entera a tu biblioteca"
                                     : "Agrega esta carpeta a tu biblioteca: sus juegos aparecer\xC3\xA1n siempre";
        ps_glyph(c, G_TRIANGLE, px + 13, by - 7, 13, 0);
        draw_text(c, FONT_BODY, 21, px + 38, by, tip, C_MUTED, 1, 0);
        draw_text(c, FONT_MONO, 16, x + w - 56 - text_width(FONT_MONO, 16, "L1 / R1  p\xC3\xA1gina", 0), by - 2, "L1 / R1  p\xC3\xA1gina", C_DIM, 1, 0);
    }
}

#include "ui_pc.inc.c"

void app_draw(App *a, Canvas *c, int hh, int mm) {
    Game *g = selected(a);
    int gi = g ? a->view[a->sel] : -1;
    if (a->bg_for != gi) {
        if (g) paint_glow_background(&a->bg, C_BG, g->c1, 0.30f, g->c2, 0.24f);
        else paint_glow_background(&a->bg, C_BG, 0x1B6CFF, 0.22f, C_ACCENT, 0.16f);
        a->bg_for = gi;
    }
    canvas_blit(c, &a->bg);
    draw_header(a, c, hh, mm);
    draw_hero(a, c);
    draw_library(a, c);
    draw_footer(a, c);
    if (a->browser.open) { draw_browser(a, c); draw_footer(a, c); }
    if (a->pc.open) { draw_pc(a, c); draw_footer(a, c); }
    if (a->modal != MODAL_NONE) { draw_modal(a, c); draw_footer(a, c); }
}

void app_draw_scanning(Canvas *c, const Canvas *bg) {
    canvas_blit(c, bg);
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0, 0.5f);
    const char *t = "Escaneando tus juegos\xE2\x80\xA6";
    float w = text_width(FONT_DISPLAY, 44, t, -1);
    draw_text(c, FONT_DISPLAY, 44, (SCREEN_W - w) / 2, SCREEN_H / 2, t, C_TEXT, 1, -1);
}

void app_launch_failed(App *a, int code) {
    a->launch_error = code;
    a->modal = plat_engine_installed() ? MODAL_LAUNCH_ERROR : MODAL_NO_ENGINE;
}

// Full-screen hand-off card shown while the console switches to M33.
void app_draw_launching(App *a, Canvas *c) {
    // The game being launched: from the library, or a file picked in Explorar.
    Game *g = NULL;
    for (int i = 0; i < a->lib.count; i++)
        if (strcmp(a->lib.games[i].path, a->launch_path) == 0) { g = &a->lib.games[i]; break; }
    canvas_blit(c, &a->bg);
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, C_BG, 0.55f);
    float cw = COVER_W, ch = COVER_H, cx = (SCREEN_W - cw) / 2, cy = 270;
    shadow_rrect(c, cx, cy + 30, cw, ch, 28, 90, 0.7f);
    int gi = g ? (int)(g - a->lib.games) : -2;
    if (g && a->cover.px && a->cover_for == gi) draw_image_rounded(c, &a->cover, (int)cx, (int)cy, 28, 1);
    else if (g && g->tile.px) { Image big = image_scale_cover(&g->tile, (int)cw, (int)ch); draw_image_rounded(c, &big, (int)cx, (int)cy, 28, 1); image_free(&big); }
    else fallback_art(c, cx, cy, cw, ch, 28, g ? g->c1 : 0x3DF5C8, g ? g->c2 : 0x1B6CFF);
    const char *title = g ? g->title : strrchr(a->launch_path, '/') ? strrchr(a->launch_path, '/') + 1 : a->launch_path;
    char t[160];
    text_ellipsize(FONT_DISPLAY, 64, title, SCREEN_W - 200, t, sizeof t, -1.5f);
    float tw = text_width(FONT_DISPLAY, 64, t, -1.5f);
    draw_text(c, FONT_DISPLAY, 64, (SCREEN_W - tw) / 2, cy + ch + 130, t, C_TEXT, 1, -1.5f);
    const char *sub = "Abriendo el motor PSP\xE2\x80\xA6";
    float sw = text_width(FONT_MONO, 20, sub, 3);
    draw_text(c, FONT_MONO, 20, (SCREEN_W - sw) / 2, cy + ch + 186, sub, C_ACCENT, 1, 3);
}

void app_launch_info(App *a, char *sys, int sys_len, char *title, int title_len) {
    Game *g = NULL;
    for (int i = 0; i < a->lib.count; i++)
        if (strcmp(a->lib.games[i].path, a->launch_path) == 0) { g = &a->lib.games[i]; break; }
    const char *slash = strrchr(a->launch_path, '/');
    snprintf(title, title_len, "%s", g ? g->title : slash ? slash + 1 : a->launch_path);
    int ps1 = g ? g->is_ps1 : 0;
    snprintf(sys, sys_len, "%s", ps1 ? "PLAYSTATION" : "PSP");
}
