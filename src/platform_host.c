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
    for (const Step *s = kScript; s->frame; s++) {
        if (s->frame != g_frame) continue;
        b |= s->buttons;
        if (s->shot) shot(s->shot);
    }
    return b;
}

void plat_present(const Canvas *c) { g_last = c; }
void plat_idle(void) {}

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
    return g_frame > 180;
}
