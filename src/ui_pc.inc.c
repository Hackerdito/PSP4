// ui_pc.inc.c - the "Mi PC" window (included by ui.c, so it shares its drawing helpers).
// Screens: setup (computer, user, password; search the network) -> shared folders -> folders and
// games -> destination (PS4 or a USB) -> download progress -> result.

#ifdef VAULT_HOST
#define PC_INTERNAL_DEST "build/host/descargas"
#else
#define PC_INTERNAL_DEST "/data/vault/games"
#endif

#define PC_ROWS 9

static void pc_start(App *a, JobKind kind, PcScreen after) {
    PcView *p = &a->pc;
    if (p->job.state == JOB_RUNNING) {   // the previous one is still finishing
        snprintf(p->msg, sizeof p->msg, "Espera un momento: la PS4 sigue hablando con la PC");
        return;
    }
    p->job.kind = kind;
    p->job.cfg = p->cfg;
    if (kind == JOB_LIST) snprintf(p->job.dir, sizeof p->job.dir, "%s", p->dir);
    p->after = after;
    p->busy = 1;
    p->ignore_result = 0;
    p->msg[0] = 0;
    pc_job_start(&p->job);
}

static void pc_open(App *a) {
    PcView *p = &a->pc;
    p->open = 1;
    p->frame = 0;
    p->kb_open = 0;
    p->msg[0] = 0;
    p->dir[0] = 0;
    p->sel = p->top = 0;
    p->scroll = 0;
    if (!pc_config_load(&p->cfg)) { p->screen = PCV_SETUP; p->field = 0; return; }
    if (p->cfg.share[0]) { p->screen = PCV_DIR; pc_start(a, JOB_LIST, PCV_DIR); }
    else { p->screen = PCV_SHARES; pc_start(a, JOB_SHARES, PCV_SHARES); }
}

static void pc_close(App *a) {
    PcView *p = &a->pc;
    if (p->job.state == JOB_RUNNING) p->job.cancel = 1;
    p->open = 0;
    if (p->downloaded) { a->scanning = 1; p->downloaded = 0; }
}

static void pc_set_list(PcView *p, const PcEntry *src, int n) {
    if (n > PC_MAX_ENTRIES) n = PC_MAX_ENTRIES;
    memcpy(p->entries, src, sizeof(PcEntry) * (size_t)n);
    p->count = n;
    p->sel = p->top = 0;
    p->scroll = 0;
}

// Places the PS4 can copy to: its own disk, and each USB drive that is plugged in.
static void pc_find_destinations(PcView *p) {
    p->dest_count = 0;
    snprintf(p->dest_name[0], sizeof p->dest_name[0], "Disco duro de la PS4");
    snprintf(p->dest_path[0], sizeof p->dest_path[0], PC_INTERNAL_DEST);
    p->dest_count = 1;
#ifndef VAULT_HOST
    for (int i = 0; i < 8 && p->dest_count < 9; i++) {
        char mnt[32];
        snprintf(mnt, sizeof mnt, "/mnt/usb%d", i);
        if (!path_is_dir(mnt)) continue;
        snprintf(p->dest_name[p->dest_count], sizeof p->dest_name[0], "Memoria USB%d", i);
        snprintf(p->dest_path[p->dest_count], sizeof p->dest_path[0], "%s/SAMPSP4", mnt);
        p->dest_count++;
    }
#endif
    p->dest_sel = 0;
}

// Job finished: move its results onto the screen.
static void pc_job_finished(App *a) {
    PcView *p = &a->pc;
    PcJob *j = &p->job;
    p->busy = 0;
    if (p->ignore_result) return;
    if (j->state == JOB_FAILED) {
        snprintf(p->msg, sizeof p->msg, "%s", j->error);
        switch (j->kind) {
        case JOB_SHARES: p->screen = PCV_SETUP; p->field = 4; break;
        case JOB_LIST:
            if (!p->dir[0]) {   // the share itself failed: back to choosing one (or the setup)
                p->screen = p->share_count ? PCV_SHARES : PCV_SETUP;
                if (p->screen == PCV_SETUP) p->field = 4;
            } else {           // a subfolder failed: stay where we were, one level up
                char *slash = strrchr(p->dir, '/');
                if (slash) *slash = 0; else p->dir[0] = 0;
            }
            break;
        case JOB_DOWNLOAD: p->screen = PCV_RESULT; break;
        case JOB_SCAN: break;
        }
        return;
    }
    switch (j->kind) {
    case JOB_SCAN:
        p->found_count = j->found_count;
        memcpy(p->found, j->found, sizeof p->found);
        p->searched = 1;
        if (!j->found_count) snprintf(p->msg, sizeof p->msg, "No encontr\xC3\xA9 PCs compartiendo archivos en la red %s", j->local_ip);
        else p->field = 5;   // first computer found
        break;
    case JOB_SHARES: {
        int n = j->count < 64 ? j->count : 64;
        memcpy(p->shares, j->entries, sizeof(PcEntry) * (size_t)n);
        p->share_count = n;
        pc_config_save(&p->cfg);   // the computer answered: keep these settings
        if (n == 0) { snprintf(p->msg, sizeof p->msg, "La PC no comparte ninguna carpeta todav\xC3\xAD" "a"); p->screen = PCV_SETUP; p->field = 4; break; }
        if (n == 1) {   // only one: open it
            snprintf(p->cfg.share, sizeof p->cfg.share, "%s", p->shares[0].name);
            pc_config_save(&p->cfg);
            p->dir[0] = 0;
            p->screen = PCV_DIR;
            pc_start(a, JOB_LIST, PCV_DIR);
            break;
        }
        pc_set_list(p, p->shares, n);
        p->screen = PCV_SHARES;
        break;
    }
    case JOB_LIST:
        pc_set_list(p, j->entries, j->count);
        p->screen = PCV_DIR;
        break;
    case JOB_DOWNLOAD:
        p->downloaded = 1;
        snprintf(p->last_dest, sizeof p->last_dest, "%s", j->dest_path);
        snprintf(p->msg, sizeof p->msg, "Listo: ya est\xC3\xA1 en tu PS4 y aparece en tu biblioteca");
        p->screen = PCV_RESULT;
        break;
    }
}

// ---------------------------------------------------------------- keyboard

static const char *kKbRows[2][4] = {
    { "1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm.-_" },
    { "!#$%&/()=?", "QWERTYUIOP", "ASDFGHJKL:", "ZXCVBNM,\\+" },
};
#define KB_ACTIONS 4   // bottom row: Mayus, Espacio, Borrar, Listo

static char *pc_field_buf(PcView *p, int field, int *len) {
    switch (field) {
    case 0: *len = sizeof p->cfg.host; return p->cfg.host;
    case 1: *len = sizeof p->cfg.user; return p->cfg.user;
    default: *len = sizeof p->cfg.pass; return p->cfg.pass;
    }
}

static void kb_open(PcView *p, int field) {
    int len;
    char *buf = pc_field_buf(p, field, &len);
    snprintf(p->kb_buf, sizeof p->kb_buf, "%s", buf);
    p->kb_field = field;
    p->kb_open = 1;
    p->kb_row = field == 0 ? 0 : 1;
    p->kb_col = 0;
    p->kb_shift = 0;
}

static void kb_commit(PcView *p) {
    int len;
    char *buf = pc_field_buf(p, p->kb_field, &len);
    char old[64];
    snprintf(old, sizeof old, "%s", buf);
    snprintf(buf, (size_t)len, "%s", p->kb_buf);
    // Trim spaces around a typed address.
    if (p->kb_field == 0) {
        char *s = buf;
        while (*s == ' ') s++;
        memmove(buf, s, strlen(s) + 1);
        size_t n = strlen(buf);
        while (n && buf[n - 1] == ' ') buf[--n] = 0;
        if (strcmp(old, buf)) p->cfg.share[0] = 0;   // another computer: choose its folder again
    }
    p->kb_open = 0;
}

static void kb_update(PcView *p, uint32_t pressed) {
    size_t n = strlen(p->kb_buf);
    int cols = p->kb_row < 4 ? 10 : KB_ACTIONS;
    if (pressed & BTN_RIGHT) p->kb_col = (p->kb_col + 1) % cols;
    if (pressed & BTN_LEFT) p->kb_col = (p->kb_col + cols - 1) % cols;
    if (pressed & (BTN_UP | BTN_DOWN)) {
        int was_actions = p->kb_row == 4;
        p->kb_row = (p->kb_row + ((pressed & BTN_DOWN) ? 1 : 4)) % 5;
        if (p->kb_row == 4) p->kb_col = p->kb_col * KB_ACTIONS / 10;
        else if (was_actions) p->kb_col = p->kb_col * 10 / KB_ACTIONS + 1;
        if (p->kb_col >= (p->kb_row < 4 ? 10 : KB_ACTIONS)) p->kb_col = 0;
    }
    if (pressed & BTN_SQUARE) { if (n) p->kb_buf[n - 1] = 0; }
    if ((pressed & BTN_TRIANGLE) && n + 1 < sizeof p->kb_buf && p->kb_field != 0) { p->kb_buf[n] = ' '; p->kb_buf[n + 1] = 0; }
    if (pressed & (BTN_L1 | BTN_R1)) p->kb_shift ^= 1;
    if (pressed & BTN_OPTIONS) { kb_commit(p); return; }
    if (pressed & BTN_CIRCLE) { p->kb_open = 0; return; }
    if (pressed & BTN_CROSS) {
        if (p->kb_row < 4) {
            if (n + 1 < sizeof p->kb_buf) { p->kb_buf[n] = kKbRows[p->kb_shift][p->kb_row][p->kb_col]; p->kb_buf[n + 1] = 0; }
        } else {
            switch (p->kb_col) {
            case 0: p->kb_shift ^= 1; break;
            case 1: if (n + 1 < sizeof p->kb_buf) { p->kb_buf[n] = ' '; p->kb_buf[n + 1] = 0; } break;
            case 2: if (n) p->kb_buf[n - 1] = 0; break;
            case 3: kb_commit(p); break;
            }
        }
    }
}

// ---------------------------------------------------------------- input

static void pc_list_move(PcView *p, uint32_t pressed, int count) {
    if ((pressed & BTN_DOWN) && p->sel < count - 1) p->sel++;
    if ((pressed & BTN_UP) && p->sel > 0) p->sel--;
    if (pressed & BTN_R1) p->sel = p->sel + PC_ROWS < count ? p->sel + PC_ROWS : (count ? count - 1 : 0);
    if (pressed & BTN_L1) p->sel = p->sel > PC_ROWS ? p->sel - PC_ROWS : 0;
}

static int pc_update(App *a, uint32_t pressed) {
    PcView *p = &a->pc;
    p->frame++;
    int dirty = (p->frame % 4) == 0 && (p->busy || p->screen == PCV_DOWNLOAD);
    if (p->busy && p->job.state != JOB_RUNNING) { pc_job_finished(a); dirty = 1; }
    if (!pressed) goto scroll;
    dirty = 1;

    if (p->kb_open) { kb_update(p, pressed); return 1; }

    if (p->busy && p->screen != PCV_DOWNLOAD) {
        // Waiting for the PC: Circle gives up on this answer.
        if (pressed & BTN_CIRCLE) {
            p->ignore_result = 1;
            p->busy = 0;
            p->job.cancel = 1;
            if (p->screen != PCV_SETUP && !p->count) pc_close(a);
        }
        return 1;
    }

    switch (p->screen) {
    case PCV_SETUP: {
        int rows = 5 + p->found_count;
        if ((pressed & BTN_DOWN) && p->field < rows - 1) p->field++;
        if ((pressed & BTN_UP) && p->field > 0) p->field--;
        if (pressed & BTN_CIRCLE) { pc_close(a); return 1; }
        if (pressed & BTN_CROSS) {
            if (p->field <= 2) kb_open(p, p->field);
            else if (p->field == 3) { p->found_count = 0; pc_start(a, JOB_SCAN, PCV_SETUP); }
            else if (p->field == 4) {
                if (!p->cfg.host[0]) { snprintf(p->msg, sizeof p->msg, "Escribe la IP de tu PC o b\xC3\xBAscala en la red"); p->field = 0; }
                else if (p->cfg.share[0]) { pc_config_save(&p->cfg); p->dir[0] = 0; p->screen = PCV_DIR; pc_start(a, JOB_LIST, PCV_DIR); }
                else pc_start(a, JOB_SHARES, PCV_SHARES);
            } else {
                int i = p->field - 5;
                if (strcmp(p->cfg.host, p->found[i])) p->cfg.share[0] = 0;
                snprintf(p->cfg.host, sizeof p->cfg.host, "%s", p->found[i]);
                p->field = p->cfg.user[0] ? 4 : 1;
            }
        }
        break;
    }
    case PCV_SHARES:
        pc_list_move(p, pressed, p->count);
        if (pressed & BTN_CIRCLE) { pc_close(a); return 1; }
        if (pressed & BTN_TRIANGLE) { p->screen = PCV_SETUP; p->field = 0; p->msg[0] = 0; }
        if ((pressed & BTN_CROSS) && p->count) {
            snprintf(p->cfg.share, sizeof p->cfg.share, "%s", p->entries[p->sel].name);
            pc_config_save(&p->cfg);
            p->dir[0] = 0;
            pc_start(a, JOB_LIST, PCV_DIR);
        }
        break;
    case PCV_DIR:
        pc_list_move(p, pressed, p->count);
        if (pressed & BTN_TRIANGLE) { p->screen = PCV_SETUP; p->field = 0; p->msg[0] = 0; }
        if (pressed & BTN_CIRCLE) {
            if (p->dir[0]) {
                char *slash = strrchr(p->dir, '/');
                if (slash) *slash = 0; else p->dir[0] = 0;
                pc_start(a, JOB_LIST, PCV_DIR);
            } else if (p->share_count > 1) {
                pc_set_list(p, p->shares, p->share_count);
                p->screen = PCV_SHARES;
            } else if (!p->share_count) {
                pc_start(a, JOB_SHARES, PCV_SHARES);   // learn the other folders, if any
            } else {
                pc_close(a);
            }
            return 1;
        }
        if ((pressed & BTN_CROSS) && p->count) {
            PcEntry *e = &p->entries[p->sel];
            if (e->kind == PC_KIND_DIR) {
                char d[512];
                if (p->dir[0]) snprintf(d, sizeof d, "%s/%s", p->dir, e->name);
                else snprintf(d, sizeof d, "%s", e->name);
                snprintf(p->dir, sizeof p->dir, "%s", d);
                pc_start(a, JOB_LIST, PCV_DIR);
            } else if (e->kind == PC_KIND_GAME) {
                snprintf(p->pick, sizeof p->pick, "%s", e->name);
                if (p->dir[0]) snprintf(p->pick_remote, sizeof p->pick_remote, "%s/%s", p->dir, e->name);
                else snprintf(p->pick_remote, sizeof p->pick_remote, "%s", e->name);
                pc_find_destinations(p);
                p->screen = PCV_DEST;
                p->msg[0] = 0;
            } else {
                snprintf(p->msg, sizeof p->msg, "Ese archivo no es un juego que SAMPSP4 abra");
            }
        }
        break;
    case PCV_DEST:
        if ((pressed & BTN_DOWN) && p->dest_sel < p->dest_count - 1) p->dest_sel++;
        if ((pressed & BTN_UP) && p->dest_sel > 0) p->dest_sel--;
        if (pressed & BTN_CIRCLE) p->screen = PCV_DIR;
        if (pressed & BTN_CROSS) {
            snprintf(p->job.remote, sizeof p->job.remote, "%s", p->pick_remote);
            snprintf(p->job.dest_dir, sizeof p->job.dest_dir, "%s", p->dest_path[p->dest_sel]);
            p->speed = 0;
            p->speed_done = 0;
            p->speed_t = plat_time_us();
            p->screen = PCV_DOWNLOAD;
            pc_start(a, JOB_DOWNLOAD, PCV_RESULT);
        }
        break;
    case PCV_DOWNLOAD:
        if ((pressed & BTN_CIRCLE) && p->busy) p->job.cancel = 1;
        break;
    case PCV_RESULT:
        if (pressed & (BTN_CROSS | BTN_CIRCLE)) { p->screen = PCV_DIR; p->msg[0] = 0; }
        break;
    }

scroll:
    if (p->screen == PCV_DIR || p->screen == PCV_SHARES) {
        if (p->sel < p->top) p->top = p->sel;
        if (p->sel >= p->top + PC_ROWS) p->top = p->sel - PC_ROWS + 1;
        float d = p->top - p->scroll;
        if (fabsf(d) > 0.02f) { p->scroll += d * 0.35f; dirty = 1; }
        else if (d != 0) { p->scroll = (float)p->top; dirty = 1; }
    }
    if (p->screen == PCV_DOWNLOAD && p->busy) {
        uint64_t t = plat_time_us();
        if (t - p->speed_t > 1000000) {
            uint64_t done = p->job.done;
            float inst = (float)(done - p->speed_done) * 1e6f / (float)(t - p->speed_t);
            p->speed = p->speed > 0 ? p->speed * 0.6f + inst * 0.4f : inst;
            p->speed_done = done;
            p->speed_t = t;
        }
    }
    return dirty;
}

// ---------------------------------------------------------------- drawing

static void pc_spinner(Canvas *c, float cx, float cy, int frame) {
    for (int i = 0; i < 8; i++) {
        float ang = (i + frame / 4) * 0.785398f;
        float al = 0.15f + 0.85f * (float)i / 7;
        fill_circle(c, cx + cosf(ang) * 18, cy + sinf(ang) * 18, 4, C_ACCENT, al);
    }
}

static void pc_row_bg(Canvas *c, float x, float y, float w, float h, int on) {
    if (on) {
        fill_rrect(c, x, y, w, h, 14, C_ACCENT, 0.12f);
        stroke_rrect(c, x, y, w, h, 14, 2, C_ACCENT, 0.7f);
    } else {
        fill_rrect(c, x, y, w, h, 14, 0xFFFFFF, 0.035f);
    }
}

static void draw_keyboard(PcView *p, Canvas *c) {
    float w = 1200, h = 590, x = (SCREEN_W - w) / 2, y = SCREEN_H - h - 92;
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, 0.45f);
    shadow_rrect(c, x, y + 24, w, h, 28, 70, 0.7f);
    fill_rrect(c, x, y, w, h, 28, 0x171A23, 1);
    stroke_rrect(c, x, y, w, h, 28, 1.5f, 0xFFFFFF, 0.12f);
    static const char *labels[3] = { "IP o nombre de la PC", "Usuario de la PC", "Contrase\xC3\xB1" "a" };
    draw_text(c, FONT_MONO, 16, x + 40, y + 46, labels[p->kb_field], C_ACCENT, 1, 3);
    fill_rrect(c, x + 40, y + 62, w - 80, 62, 14, 0x000000, 0.35f);
    char shown[80];
    snprintf(shown, sizeof shown, "%s", p->kb_buf);
    float tw = draw_text(c, FONT_DISPLAY, 30, x + 60, y + 104, shown, C_TEXT, 1, 0);
    if ((p->frame / 20) % 2 == 0) fill_rect(c, (int)(x + 64 + tw), (int)(y + 76), 3, 34, C_ACCENT, 1);
    float kw = 100, kh = 62, gap = 10, kx0 = x + (w - (10 * kw + 9 * gap)) / 2, ky0 = y + 148;
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 10; k++) {
            float kx = kx0 + k * (kw + gap), ky = ky0 + r * (kh + gap);
            int on = p->kb_row == r && p->kb_col == k;
            fill_rrect(c, kx, ky, kw, kh, 12, on ? C_ACCENT : 0xFFFFFF, on ? 1 : 0.07f);
            char ch[2] = { kKbRows[p->kb_shift][r][k], 0 };
            float cw = text_width(FONT_DISPLAY, 28, ch, 0);
            draw_text(c, FONT_DISPLAY, 28, kx + (kw - cw) / 2, ky + 42, ch, on ? C_INK : C_TEXT, 1, 0);
        }
    static const char *acts[KB_ACTIONS] = { "May\xC3\xBAs", "Espacio", "Borrar", "Listo" };
    float aw = (10 * kw + 9 * gap - 3 * gap) / KB_ACTIONS, ay = ky0 + 4 * (kh + gap);
    for (int i = 0; i < KB_ACTIONS; i++) {
        float ax = kx0 + i * (aw + gap);
        int on = p->kb_row == 4 && p->kb_col == i;
        int lit = (i == 0 && p->kb_shift) || i == 3;
        fill_rrect(c, ax, ay, aw, kh, 12, on ? C_ACCENT : lit ? 0x3F7BFF : 0xFFFFFF, on ? 1 : lit ? 0.35f : 0.07f);
        float cw = text_width(FONT_DISPLAY, 24, acts[i], 0);
        draw_text(c, FONT_DISPLAY, 24, ax + (aw - cw) / 2, ay + 40, acts[i], on ? C_INK : C_TEXT, 1, 0);
    }
    const char *tip = "\xE2\x96\xA1 Borrar   \xE2\x96\xB3 Espacio   L1/R1 May\xC3\xBAs   OPTIONS Listo   \xE2\x97\x8B Cancelar";
    (void)tip;
    float hx = x + 40, hy = y + h - 22;
    ps_glyph(c, G_SQUARE, hx + 12, hy - 7, 12, 0); hx += draw_text(c, FONT_BODY, 19, hx + 30, hy, "Borrar", C_MUTED, 1, 0) + 54;
    ps_glyph(c, G_TRIANGLE, hx + 12, hy - 7, 12, 0); hx += draw_text(c, FONT_BODY, 19, hx + 30, hy, "Espacio", C_MUTED, 1, 0) + 54;
    hx += key_chip(c, hx, hy - 7, "L1/R1") + 10; hx += draw_text(c, FONT_BODY, 19, hx, hy, "May\xC3\xBAs", C_MUTED, 1, 0) + 40;
    hx += key_chip(c, hx, hy - 7, "OPTIONS") + 10; hx += draw_text(c, FONT_BODY, 19, hx, hy, "Listo", C_MUTED, 1, 0) + 40;
    ps_glyph(c, G_CIRCLE, hx + 12, hy - 7, 12, 0); draw_text(c, FONT_BODY, 19, hx + 30, hy, "Cancelar", C_MUTED, 1, 0);
}

static void pc_draw_setup(PcView *p, Canvas *c, float px, float py, float pw) {
    draw_text(c, FONT_BODY, 22, px, py, "Comparte una carpeta en tu PC (Windows o Mac) y escribe aqu\xC3\xAD sus datos:", C_SOFT, 1, 0);
    static const char *labels[3] = { "PC (IP o nombre)", "Usuario", "Contrase\xC3\xB1" "a" };
    float fy = py + 34;
    for (int i = 0; i < 3; i++) {
        float ry = fy + i * 76;
        pc_row_bg(c, px - 12, ry, pw + 24, 64, p->field == i);
        draw_text(c, FONT_MONO, 17, px + 10, ry + 40, labels[i], C_MUTED, 1, 0);
        char v[80];
        if (i == 2) { size_t n = strlen(p->cfg.pass); if (n > 40) n = 40; memset(v, 0, sizeof v); for (size_t k = 0; k < n; k++) strcat(v, "\xE2\x80\xA2"); }
        else snprintf(v, sizeof v, "%s", i == 0 ? p->cfg.host : p->cfg.user);
        if (!v[0]) draw_text(c, FONT_BODY, 24, px + 300, ry + 41, i == 0 ? "p. ej. 192.168.1.50" : i == 1 ? "tu usuario de Windows o Mac" : "", C_DIM, 1, 0);
        else draw_text(c, FONT_DISPLAY, 26, px + 300, ry + 42, v, C_TEXT, 1, 0);
    }
    float by = fy + 3 * 76 + 10;
    static const char *btn[2] = { "Buscar PCs en la red", "Conectar" };
    float bx = px - 12;
    for (int i = 0; i < 2; i++) {
        int on = p->field == 3 + i;
        float bw = text_width(FONT_DISPLAY, 24, btn[i], 0) + 64;
        fill_rrect(c, bx, by, bw, 60, 16, on ? (i ? C_ACCENT : C_TEXT) : 0xFFFFFF, on ? 1 : 0.08f);
        draw_text(c, FONT_DISPLAY, 24, bx + 32, by + 39, btn[i], on ? C_INK : C_TEXT, 1, 0);
        bx += bw + 16;
    }
    float ly = by + 96;
    if (p->found_count) {
        draw_text(c, FONT_MONO, 16, px, ly, "PCS ENCONTRADAS", C_ACCENT, 1, 3);
        for (int i = 0; i < p->found_count && i < 4; i++) {
            float ry = ly + 16 + i * 60;
            pc_row_bg(c, px - 12, ry, pw + 24, 52, p->field == 5 + i);
            icon_drive(c, px + 4, ry + 8, C_SOFT);
            draw_text(c, FONT_DISPLAY, 24, px + 60, ry + 35, p->found[i], C_TEXT, 1, 0);
            if (!strcmp(p->found[i], p->cfg.host)) draw_text(c, FONT_MONO, 16, px + pw - 120, ry + 33, "ELEGIDA", C_ACCENT, 1, 2);
        }
    } else {
        draw_text(c, FONT_BODY, 21, px, ly, "\xC2\xBF" "C\xC3\xB3mo compartir? Windows: clic derecho en la carpeta > Propiedades > Compartir.", C_MUTED, 1, 0);
        draw_text(c, FONT_BODY, 21, px, ly + 34, "Mac: Ajustes > General > Compartir > Compartir archivos (activa SMB y tu usuario en Opciones).", C_MUTED, 1, 0);
        draw_text(c, FONT_BODY, 21, px, ly + 68, "La IP de la PC: en Windows escribe ipconfig; en Mac, Ajustes > Red.", C_MUTED, 1, 0);
    }
}

static void pc_draw_list(PcView *p, Canvas *c, float px, float py, float pw) {
    const float row_h = 62;
    if (!p->count) {
        draw_text(c, FONT_BODY, 24, px, py + 60, p->screen == PCV_SHARES ? "La PC no comparte carpetas." : "Carpeta vac\xC3\xAD" "a.", C_MUTED, 1, 0);
        return;
    }
    for (int i = 0; i < p->count; i++) {
        float rel = i - p->scroll;
        if (rel < -0.5f || rel > PC_ROWS - 0.5f) continue;
        float ry = py + rel * row_h;
        PcEntry *e = &p->entries[i];
        int on = i == p->sel;
        if (on) {
            fill_rrect(c, px - 12, ry, pw + 24, row_h - 6, 14, C_ACCENT, 0.12f);
            stroke_rrect(c, px - 12, ry, pw + 24, row_h - 6, 14, 2, C_ACCENT, 0.7f);
        }
        float iy = ry + (row_h - 6) / 2 - 17;
        DiscFormat ef = (DiscFormat)disc_format_from_name(e->name);
        switch (e->kind) {
        case PC_KIND_SHARE: icon_drive(c, px + 4, iy, C_SOFT); break;
        case PC_KIND_DIR: icon_folder(c, px + 4, iy, C_SOFT); break;
        case PC_KIND_GAME: icon_disc(c, px + 4, iy, fmt_color(ef)); break;
        default: icon_file(c, px + 4, iy, C_DIM); break;
        }
        char note[32] = "";
        if (e->kind == PC_KIND_GAME || e->kind == PC_KIND_FILE) format_size(e->size, note, sizeof note);
        else snprintf(note, sizeof note, e->kind == PC_KIND_SHARE ? "Carpeta compartida" : "Carpeta");
        float note_w = text_width(FONT_MONO, 18, note, 0);
        char name[200];
        FontId f = e->kind == PC_KIND_FILE ? FONT_BODY : FONT_DISPLAY;
        text_ellipsize(f, 24, e->name, pw - 120 - note_w - 140, name, sizeof name, 0);
        float base = ry + (row_h - 6) / 2 + 8;
        float nw = draw_text(c, f, 24, px + 60, base, name, e->kind == PC_KIND_FILE ? C_DIM : C_TEXT, 1, 0);
        if (e->kind == PC_KIND_GAME)
            pill(c, px + 60 + nw + 16, ry + (row_h - 6) / 2 - 13, fmt_name(ef), fmt_color(ef), 1, C_INK, FONT_MONO, 14, 9, 26);
        draw_text(c, FONT_MONO, 18, px + pw - note_w, base - 1, note, C_MUTED, 1, 0);
    }
}

static void pc_draw_progress(PcView *p, Canvas *c, float px, float py, float pw) {
    PcJob *j = &p->job;
    char t[200];
    text_ellipsize(FONT_DISPLAY, 40, p->pick, pw, t, sizeof t, -0.5f);
    draw_text(c, FONT_DISPLAY, 40, px, py + 50, t, C_TEXT, 1, -0.5f);
    char where[200];
    snprintf(where, sizeof where, "a %s", p->dest_name[p->dest_sel]);
    draw_text(c, FONT_BODY, 22, px, py + 92, where, C_MUTED, 1, 0);
    uint64_t total = j->total, done = j->done;
    float f = total ? (float)((double)done / (double)total) : 0;
    if (f > 1) f = 1;
    float by = py + 150;
    fill_rrect(c, px, by, pw, 22, 11, 0xFFFFFF, 0.08f);
    if (f > 0) fill_rrect(c, px, by, fmaxf(22, pw * f), 22, 11, C_ACCENT, 1);
    char a1[24], a2[24], line[160];
    format_size(done, a1, sizeof a1);
    format_size(total, a2, sizeof a2);
    snprintf(line, sizeof line, "%d%%  \xC2\xB7  %s de %s", (int)(f * 100), a1, a2);
    draw_text(c, FONT_MONO, 20, px, by + 62, line, C_SOFT, 1, 0);
    if (p->speed > 1) {
        char sp[24], eta[48] = "";
        format_size((uint64_t)p->speed, sp, sizeof sp);
        if (total > done) {
            int s = (int)((double)(total - done) / p->speed);
            snprintf(eta, sizeof eta, "  \xC2\xB7  faltan %d:%02d", s / 60, s % 60);
        }
        char right[96];
        snprintf(right, sizeof right, "%s/s%s", sp, eta);
        float rw = text_width(FONT_MONO, 20, right, 0);
        draw_text(c, FONT_MONO, 20, px + pw - rw, by + 62, right, C_MUTED, 1, 0);
    }
    if (j->file_count > 1) {
        char fl[260];
        snprintf(fl, sizeof fl, "Archivo %d de %d: %s", j->file_index + 1, j->file_count, j->current);
        draw_text(c, FONT_BODY, 21, px, by + 110, fl, C_MUTED, 1, 0);
    }
    draw_text(c, FONT_BODY, 21, px, by + 170, j->cancel ? "Cancelando\xE2\x80\xA6" : "Puedes cancelar con \xE2\x97\x8B; la PS4 borra lo que alcanz\xC3\xB3 a copiar.", C_DIM, 1, 0);
}

static void draw_pc(App *a, Canvas *c) {
    PcView *p = &a->pc;
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, 0.66f);
    float w = 1600, h = 872, x = (SCREEN_W - w) / 2, y = 88;
    shadow_rrect(c, x, y + 30, w, h, 32, 90, 0.7f);
    fill_rrect(c, x, y, w, h, 32, C_PANEL, 1);
    stroke_rrect(c, x, y, w, h, 32, 1.5f, 0xFFFFFF, 0.1f);
    float px = x + 56, pw = w - 112;

    // Header: where we are.
    draw_text(c, FONT_MONO, 18, px, y + 62, "MI PC", C_ACCENT, 1, 3.6f);
    char head[400], hs[300];
    switch (p->screen) {
    case PCV_SETUP: snprintf(head, sizeof head, "Conectar con tu PC"); break;
    case PCV_SHARES: snprintf(head, sizeof head, "%s", p->cfg.host); break;
    case PCV_DEST: snprintf(head, sizeof head, "\xC2\xBF" "D\xC3\xB3nde lo guardo?"); break;
    case PCV_DOWNLOAD: snprintf(head, sizeof head, "Descargando"); break;
    case PCV_RESULT: snprintf(head, sizeof head, p->job.state == JOB_DONE ? "Descarga lista" : "No se pudo descargar"); break;
    default: {
        char d[300];
        snprintf(d, sizeof d, "%s", p->dir);
        for (char *q = d; *q; q++) if (*q == '/') *q = 0x01;   // separators drawn as " / " below
        char out[400];
        int o = snprintf(out, sizeof out, "%s", p->cfg.share);
        if (d[0]) {
            o += snprintf(out + o, sizeof out - o, " / ");
            for (char *q = d; *q && o < (int)sizeof out - 4; q++) {
                if (*q == 0x01) o += snprintf(out + o, sizeof out - o, " / ");
                else out[o++] = *q, out[o] = 0;
            }
        }
        snprintf(head, sizeof head, "%s", out);
    }
    }
    text_ellipsize(FONT_DISPLAY, 38, head, pw - 380, hs, sizeof hs, -0.5f);
    draw_text(c, FONT_DISPLAY, 38, px - 2, y + 112, hs, C_TEXT, 1, -0.5f);
    if (p->screen == PCV_DIR || p->screen == PCV_SHARES) {
        char info[96];
        if (p->screen == PCV_DIR) {
            int g = 0;
            for (int i = 0; i < p->count; i++) g += p->entries[i].kind == PC_KIND_GAME;
            snprintf(info, sizeof info, "%s \xC2\xB7 %d juego%s", p->cfg.host, g, g == 1 ? "" : "s");
        } else {
            snprintf(info, sizeof info, "%d carpeta%s compartida%s", p->count, p->count == 1 ? "" : "s", p->count == 1 ? "" : "s");
        }
        float iw = text_width(FONT_MONO, 18, info, 0);
        draw_text(c, FONT_MONO, 18, x + w - 56 - iw, y + 104, info, C_MUTED, 1, 0);
    }
    fill_rect(c, (int)px, (int)(y + 140), (int)pw, 1, 0xFFFFFF, 0.08f);
    float py = y + 182;

    switch (p->screen) {
    case PCV_SETUP: pc_draw_setup(p, c, px, py, pw); break;
    case PCV_SHARES:
    case PCV_DIR: pc_draw_list(p, c, px, py - 26, pw); break;
    case PCV_DEST: {
        char t[200];
        text_ellipsize(FONT_DISPLAY, 30, p->pick, pw, t, sizeof t, 0);
        draw_text(c, FONT_DISPLAY, 30, px, py + 10, t, C_TEXT, 1, 0);
        for (int i = 0; i < p->dest_count; i++) {
            float ry = py + 50 + i * 76;
            pc_row_bg(c, px - 12, ry, pw + 24, 64, p->dest_sel == i);
            if (i == 0) icon_folder(c, px + 4, ry + 14, C_SOFT); else icon_drive(c, px + 4, ry + 14, C_SOFT);
            draw_text(c, FONT_DISPLAY, 26, px + 60, ry + 42, p->dest_name[i], C_TEXT, 1, 0);
            float nw = text_width(FONT_MONO, 17, p->dest_path[i], 0);
            draw_text(c, FONT_MONO, 17, px + pw - nw, ry + 40, p->dest_path[i], C_MUTED, 1, 0);
        }
        draw_text(c, FONT_BODY, 21, px, py + 60 + p->dest_count * 76 + 30,
                  "Se copia a tu PS4; el original se queda en la PC.", C_MUTED, 1, 0);
        break;
    }
    case PCV_DOWNLOAD: pc_draw_progress(p, c, px, py, pw); break;
    case PCV_RESULT: {
        int ok = p->job.state == JOB_DONE;
        fill_circle(c, px + 34, py + 54, 30, ok ? C_ACCENT : 0xFF6B6B, 0.18f);
        if (ok) { draw_line(c, px + 20, py + 56, px + 31, py + 67, 4, C_ACCENT, 1); draw_line(c, px + 31, py + 67, px + 50, py + 42, 4, C_ACCENT, 1); }
        else { draw_line(c, px + 22, py + 42, px + 46, py + 66, 4, 0xFF6B6B, 1); draw_line(c, px + 46, py + 42, px + 22, py + 66, 4, 0xFF6B6B, 1); }
        char t[200];
        text_ellipsize(FONT_DISPLAY, 34, p->pick, pw - 100, t, sizeof t, 0);
        draw_text(c, FONT_DISPLAY, 34, px + 90, py + 50, t, C_TEXT, 1, 0);
        draw_text(c, FONT_BODY, 24, px + 90, py + 92, p->msg, ok ? C_ACCENT : 0xFF8A80, 1, 0);
        if (ok) {
            char d[400], dd[300];
            browser_display_path(p->last_dest, d, sizeof d);
            text_ellipsize(FONT_MONO, 18, d, pw - 100, dd, sizeof dd, 0);
            draw_text(c, FONT_MONO, 18, px + 90, py + 132, dd, C_MUTED, 1, 0);
        }
        break;
    }
    }

    // Bottom line: errors / tips.
    float by = y + h - 40;
    fill_rect(c, (int)px, (int)(by - 44), (int)pw, 1, 0xFFFFFF, 0.08f);
    if (p->msg[0] && p->screen != PCV_RESULT) draw_text(c, FONT_BODY, 22, px, by, p->msg, 0xFF8A80, 1, 0);
    else if (p->screen == PCV_DIR || p->screen == PCV_SHARES) {
        ps_glyph(c, G_TRIANGLE, px + 13, by - 7, 13, 0);
        draw_text(c, FONT_BODY, 21, px + 38, by, "Cambiar de PC, usuario o contrase\xC3\xB1" "a", C_MUTED, 1, 0);
    }

    // Waiting for the PC.
    if (p->busy && p->screen != PCV_DOWNLOAD) {
        fill_rrect(c, x + w / 2 - 300, y + h / 2 - 70, 600, 140, 24, 0x0B0D13, 0.94f);
        pc_spinner(c, x + w / 2 - 210, y + h / 2, p->frame);
        const char *t = p->job.kind == JOB_SCAN ? "Buscando PCs en tu red\xE2\x80\xA6" : "Conectando con la PC\xE2\x80\xA6";
        draw_text(c, FONT_DISPLAY, 26, x + w / 2 - 170, y + h / 2 + 9, t, C_TEXT, 1, 0);
    }
    if (p->kb_open) draw_keyboard(p, c);
}
