// main.c - VAULT entry point and frame loop.
#include <stdio.h>
#include <stdlib.h>

#include "gfx.h"
#include "platform.h"
#include "ui.h"

static App g_app;

int main(void) {
    char fonts[256];
    snprintf(fonts, sizeof fonts, "%s/assets/fonts", plat_app_dir());
    // Fonts live inside the package, so load them before escaping the sandbox.
    int nf = fonts_load(fonts);
    if (!plat_init()) {
        for (;;) {}
    }
    plat_log("fonts loaded: %d", nf);

    Canvas frame = canvas_new(SCREEN_W, SCREEN_H);
    App *a = &g_app;
    app_init(a);
    int hh = 0, mm = 0;

    // First frame: scanning overlay, then the real scan.
    app_draw(a, &frame, hh, mm);
    app_draw_scanning(&frame, &a->bg);
    plat_present(&frame);
    app_rescan(a);

    uint32_t prev = 0;
    int repeat = 0, dirty = 1, last_min = -1;
    for (;;) {
        uint32_t held = plat_buttons();
        uint32_t pressed = held & ~prev;
        // Auto-repeat for left/right.
        if (held & (BTN_LEFT | BTN_RIGHT)) {
            if (++repeat > 18 && repeat % 5 == 0) pressed |= held & (BTN_LEFT | BTN_RIGHT);
        } else {
            repeat = 0;
        }
        prev = held;

        if (app_update(a, pressed, held)) dirty = 1;
        if (a->launch_request) {
            a->launch_request = 0;
            app_draw_launching(a, &frame);
            plat_present(&frame);
            int rc = plat_launch_engine(a->launch_path);
            if (rc != 0) app_launch_failed(a, rc);
            dirty = 1;
        }
        if (a->scanning) {
            app_draw_scanning(&frame, &a->bg);
            plat_present(&frame);
            app_rescan(a);
            a->scanning = 0;
            dirty = 1;
        }
        plat_clock(&hh, &mm);
        if (mm != last_min) { last_min = mm; dirty = 1; }

        if (dirty) {
            app_draw(a, &frame, hh, mm);
            plat_present(&frame);
            dirty = 0;
        } else {
            plat_idle();   // nothing changed: keep the last flipped frame on screen
        }
#ifdef VAULT_HOST
        if (plat_host_done()) break;
#endif
    }
    return 0;
}
