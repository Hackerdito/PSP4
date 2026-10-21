// ui_upload.inc.c - pantalla "Subir desde el celular" (R2). Incluido por ui.c.
//
// Mientras está abierta corre el servidor de upload.c: el celular escanea el QR, escribe el PIN y
// sube juegos por WiFi. Al cerrarla se detiene el servidor y, si llegó algo, se vuelve a escanear la
// biblioteca. Una subida que quedó a medias se reanuda la próxima vez que se abra.
#include "upload.h"
#include "qrcodegen/qrcodegen.h"

static void up_open(App *a) {
    UploadView *u = &a->upload;
    u->open = 1;
    u->frame = 0;
    u->added_at_open = upload_status()->games_added;
    upload_start();
    u->qr_for[0] = 0;   // el QR se arma al dibujar, con la URL que haya
}

static void up_close(App *a) {
    UploadView *u = &a->upload;
    upload_stop();
    u->open = 0;
    if (upload_status()->games_added != u->added_at_open) a->scanning = 1;
}

static int up_update(App *a, uint32_t pressed) {
    UploadView *u = &a->upload;
    const UploadStatus *st = upload_status();
    u->frame++;
    if (pressed & BTN_CIRCLE) { up_close(a); return 1; }
    // Volver a dibujar solo cuando algo cambia (o 4 veces por segundo durante una subida).
    uint64_t sig = (uint64_t)st->games_added * 1000003u + (uint64_t)st->paired * 101u + (uint64_t)st->active +
                   (st->total ? st->done * 1000 / st->total : 0) * 7919u + (uint64_t)(st->url[0] != 0) * 3u;
    int dirty = sig != u->last_sig || (st->active && u->frame % 15 == 0);
    u->last_sig = sig;
    return dirty || pressed;
}

static void up_draw_qr(UploadView *u, Canvas *c, const char *text, float x, float y, float size) {
    if (strcmp(u->qr_for, text) != 0) {
        uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
        u->qr_ok = qrcodegen_encodeText(text, tmp, u->qr, qrcodegen_Ecc_MEDIUM, 1, 6, qrcodegen_Mask_AUTO, true);
        snprintf(u->qr_for, sizeof u->qr_for, "%s", text);
    }
    fill_rrect(c, x, y, size, size, 28, 0xFFFFFF, 1);
    if (!u->qr_ok) return;
    int n = qrcodegen_getSize(u->qr);
    float quiet = 4, cell = (size - 32) / (n + quiet * 2);
    float ox = x + (size - cell * n) / 2, oy = y + (size - cell * n) / 2;
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++)
            if (qrcodegen_getModule(u->qr, i, j))
                fill_rect(c, (int)(ox + i * cell), (int)(oy + j * cell), (int)(ox + (i + 1) * cell) - (int)(ox + i * cell),
                          (int)(oy + (j + 1) * cell) - (int)(oy + j * cell), 0x000000, 1);
}

static void up_step(Canvas *c, float x, float y, int n, const char *text) {
    char num[4];
    snprintf(num, sizeof num, "%d", n);
    fill_circle(c, x + 20, y - 8, 20, C_BRAND, 1);
    float tw = text_width(FONT_DISPLAY, 22, num, 0);
    draw_text(c, FONT_DISPLAY, 22, x + 20 - tw / 2, y, num, 0xFFFFFF, 1, 0);
    draw_text(c, FONT_BODY, 26, x + 60, y, text, C_SOFT, 1, 0);
}

static void draw_upload(App *a, Canvas *c) {
    UploadView *u = &a->upload;
    const UploadStatus *st = upload_status();
    fill_rect(c, 0, 0, SCREEN_W, SCREEN_H, 0x000000, 0.66f);
    float w = 1600, h = 872, x = (SCREEN_W - w) / 2, y = 88;
    shadow_rrect(c, x, y + 30, w, h, 32, 90, 0.7f);
    fill_rrect(c, x, y, w, h, 32, C_PANEL, 1);
    stroke_rrect(c, x, y, w, h, 32, 1.5f, 0xFFFFFF, 0.1f);
    float px = x + 56;

    draw_text(c, FONT_MONO, 18, px, y + 62, "SUBIR DESDE EL CELULAR", C_ACCENT, 1, 3.6f);
    draw_text(c, FONT_DISPLAY, 38, px - 2, y + 112, "Agrega juegos desde tu celular o tu PC", C_TEXT, 1, -0.5f);
    fill_rect(c, (int)px, (int)(y + 140), (int)(w - 112), 1, 0xFFFFFF, 0.08f);

    if (!st->running || !st->url[0]) {
        const char *why = st->error[0] ? st->error : "No se pudo iniciar";
        draw_text(c, FONT_DISPLAY, 32, px, y + 260, why, 0xFF8A80, 1, 0);
        draw_text(c, FONT_BODY, 24, px, y + 310, "Revisa que la PS4 est\xC3\xA9 conectada a tu WiFi o por cable y vuelve a abrir esta pantalla.", C_MUTED, 1, 0);
        return;
    }

    // Izquierda: el QR con la dirección.
    float qs = 420, qx = px, qy = y + 186;
    up_draw_qr(u, c, st->url, qx, qy, qs);

    // Derecha: los pasos, la dirección y el PIN.
    float rx = qx + qs + 80, ry = y + 230;
    up_step(c, rx, ry, 1, "Conecta tu celular al mismo WiFi que la PS4");
    up_step(c, rx, ry + 62, 2, "Escanea el c\xC3\xB3" "digo, o abre en el navegador:");
    draw_text(c, FONT_MONO, 34, rx + 60, ry + 118, st->url, C_TEXT, 1, 0);
    up_step(c, rx, ry + 190, 3, "Escribe este PIN:");
    float kx = rx + 60, ky = ry + 222;
    for (int i = 0; i < 6 && st->pin[i]; i++) {
        char d[2] = { st->pin[i], 0 };
        fill_rrect(c, kx + i * 92, ky, 76, 96, 18, 0xFFFFFF, 0.06f);
        stroke_rrect(c, kx + i * 92, ky, 76, 96, 18, 1.5f, C_BRAND, 0.6f);
        float dw = text_width(FONT_DISPLAY, 56, d, 0);
        draw_text(c, FONT_DISPLAY, 56, kx + i * 92 + 38 - dw / 2, ky + 70, d, C_TEXT, 1, 0);
    }

    // Abajo: lo que está pasando.
    float sy = y + h - 170, sw = w - 112;
    fill_rrect(c, px, sy, sw, 112, 20, 0xFFFFFF, 0.035f);
    char line[300];
    int added = st->games_added - u->added_at_open;
    if (st->active && st->total) {
        float t = (float)st->done / (float)st->total;
        char name[200];
        text_ellipsize(FONT_BODY, 24, st->current, sw - 260, name, sizeof name, 0);
        snprintf(line, sizeof line, "Recibiendo %s", name);
        draw_text(c, FONT_BODY, 24, px + 28, sy + 44, line, C_TEXT, 1, 0);
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)(t * 100));
        float pw = text_width(FONT_MONO, 22, pct, 0);
        draw_text(c, FONT_MONO, 22, px + sw - 28 - pw, sy + 44, pct, C_ACCENT, 1, 0);
        fill_rrect(c, px + 28, sy + 68, sw - 56, 12, 6, 0xFFFFFF, 0.08f);
        if (t > 0) fill_rrect(c, px + 28, sy + 68, (sw - 56) * (t > 1 ? 1 : t), 12, 6, C_ACCENT, 1);
    } else if (added > 0) {
        // Palomita dibujada: las fuentes de SAMPSP4 no traen el carácter.
        draw_line(c, px + 30, sy + 42, px + 38, sy + 50, 3, C_ACCENT, 1);
        draw_line(c, px + 38, sy + 50, px + 52, sy + 34, 3, C_ACCENT, 1);
        snprintf(line, sizeof line, "%d cambio%s en tu biblioteca \xC2\xB7 \xC3\xBAltimo: %s", added, added == 1 ? "" : "s", st->last_added);
        draw_text(c, FONT_BODY, 24, px + 66, sy + 50, line, C_ACCENT, 1, 0);
        draw_text(c, FONT_BODY, 20, px + 28, sy + 84, "Al cerrar esta pantalla aparecen en su secci\xC3\xB3n.", C_MUTED, 1, 0);
    } else {
        draw_text(c, FONT_BODY, 24, px + 28, sy + 50, st->paired ? "Celular conectado. Elige tus juegos en la web." : "Esperando al celular\xE2\x80\xA6", C_SOFT, 1, 0);
        draw_text(c, FONT_BODY, 20, px + 28, sy + 84, "Formatos: ISO, CSO, PBP, CHD y CUE + BIN. Con cable de red las subidas son m\xC3\xA1s r\xC3\xA1pidas.", C_MUTED, 1, 0);
    }
    draw_text(c, FONT_BODY, 20, px, y + h - 30, "No cierres esta pantalla mientras se sube un juego. Si se corta, la subida sigue donde iba.", C_DIM, 1, 0);
}
