// gfx.c - software rendering primitives, image decoding and TrueType text.
#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBI_NO_THREAD_LOCALS
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }

uint32_t hex(uint32_t rgb) { return rgb & 0xFFFFFF; }

static inline uint32_t blend(uint32_t d, uint32_t s, float a) {
    if (a >= 0.999f) return s;
    int ia = (int)(a * 256.0f);
    int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
    int sr = (s >> 16) & 255, sg = (s >> 8) & 255, sb = s & 255;
    dr += ((sr - dr) * ia) >> 8;
    dg += ((sg - dg) * ia) >> 8;
    db += ((sb - db) * ia) >> 8;
    return (uint32_t)((dr << 16) | (dg << 8) | db);
}

uint32_t mix(uint32_t a, uint32_t b, float t) { return blend(a, b, clampf(t, 0, 1)); }

static inline void put(Canvas *c, int x, int y, uint32_t color, float a) {
    if (a <= 0.003f || (unsigned)x >= (unsigned)c->w || (unsigned)y >= (unsigned)c->h) return;
    uint32_t *p = &c->px[y * c->w + x];
    *p = blend(*p, color, a);
}

Canvas canvas_new(int w, int h) {
    Canvas c = { w, h, calloc((size_t)w * h, 4) };
    return c;
}

void canvas_blit(Canvas *dst, const Canvas *src) {
    memcpy(dst->px, src->px, (size_t)src->w * src->h * 4);
}

void fill_rect(Canvas *c, int x, int y, int w, int h, uint32_t color, float alpha) {
    int x0 = maxi(x, 0), y0 = maxi(y, 0), x1 = mini(x + w, c->w), y1 = mini(y + h, c->h);
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->px + j * c->w;
        if (alpha >= 0.999f) for (int i = x0; i < x1; i++) row[i] = color;
        else for (int i = x0; i < x1; i++) row[i] = blend(row[i], color, alpha);
    }
}

// Signed distance from pixel centre (px,py) to a rounded box.
static inline float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r) {
    float qx = fabsf(px - cx) - (hw - r);
    float qy = fabsf(py - cy) - (hh - r);
    if (qx > 0 && qy > 0) return sqrtf(qx * qx + qy * qy) - r;
    return (qx > qy ? qx : qy) - r;
}

void fill_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t color, float alpha) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    float cx = x + w / 2, cy = y + h / 2;
    int x0 = maxi((int)floorf(x), 0), y0 = maxi((int)floorf(y), 0);
    int x1 = mini((int)ceilf(x + w), c->w), y1 = mini((int)ceilf(y + h), c->h);
    for (int j = y0; j < y1; j++) {
        float py = j + 0.5f;
        int inner_row = py > y + r + 1 && py < y + h - r - 1;
        for (int i = x0; i < x1; i++) {
            float px = i + 0.5f;
            float cov;
            if (inner_row && px > x + 1 && px < x + w - 1) cov = 1.0f;
            else cov = clampf(0.5f - sd_rrect(px, py, cx, cy, w / 2, h / 2, r), 0, 1);
            if (cov > 0) {
                uint32_t *p = &c->px[j * c->w + i];
                *p = blend(*p, color, cov * alpha);
            }
        }
    }
}

void stroke_rrect(Canvas *c, float x, float y, float w, float h, float r, float t, uint32_t color, float alpha) {
    float cx = x + w / 2, cy = y + h / 2;
    int x0 = maxi((int)floorf(x - 1), 0), y0 = maxi((int)floorf(y - 1), 0);
    int x1 = mini((int)ceilf(x + w + 1), c->w), y1 = mini((int)ceilf(y + h + 1), c->h);
    for (int j = y0; j < y1; j++) {
        float py = j + 0.5f;
        for (int i = x0; i < x1; i++) {
            float px = i + 0.5f;
            // Skip the interior quickly.
            if (px > x + t + r && px < x + w - t - r && py > y + t + 1 && py < y + h - t - 1) {
                int skip = (int)(x + w - t - r) - 1;
                if (skip > i) i = skip;
                continue;
            }
            float sd = sd_rrect(px, py, cx, cy, w / 2, h / 2, r);
            float cov = clampf(0.5f - (fabsf(sd + t / 2) - t / 2), 0, 1);
            if (cov > 0) put(c, i, j, color, cov * alpha);
        }
    }
}

void fill_circle(Canvas *c, float cx, float cy, float r, uint32_t color, float alpha) {
    int x0 = maxi((int)(cx - r - 1), 0), x1 = mini((int)(cx + r + 2), c->w);
    int y0 = maxi((int)(cy - r - 1), 0), y1 = mini((int)(cy + r + 2), c->h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            float cov = clampf(r - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
            if (cov > 0) put(c, i, j, color, cov * alpha);
        }
}

void stroke_circle(Canvas *c, float cx, float cy, float r, float t, uint32_t color, float alpha) {
    int x0 = maxi((int)(cx - r - 2), 0), x1 = mini((int)(cx + r + 3), c->w);
    int y0 = maxi((int)(cy - r - 2), 0), y1 = mini((int)(cy + r + 3), c->h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - r);
            float cov = clampf(t / 2 - d + 0.5f, 0, 1);
            if (cov > 0) put(c, i, j, color, cov * alpha);
        }
}

void draw_line(Canvas *c, float x0, float y0, float x1, float y1, float t, uint32_t color, float alpha) {
    float minx = fminf(x0, x1) - t, maxx = fmaxf(x0, x1) + t;
    float miny = fminf(y0, y1) - t, maxy = fmaxf(y0, y1) + t;
    float vx = x1 - x0, vy = y1 - y0, vv = vx * vx + vy * vy;
    if (vv < 1e-6f) vv = 1e-6f;
    for (int j = maxi((int)miny, 0); j < mini((int)maxy + 1, c->h); j++)
        for (int i = maxi((int)minx, 0); i < mini((int)maxx + 1, c->w); i++) {
            float px = i + 0.5f - x0, py = j + 0.5f - y0;
            float h = clampf((px * vx + py * vy) / vv, 0, 1);
            float dx = px - vx * h, dy = py - vy * h;
            float cov = clampf(t / 2 - sqrtf(dx * dx + dy * dy) + 0.5f, 0, 1);
            if (cov > 0) put(c, i, j, color, cov * alpha);
        }
}

void fill_triangle(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color, float alpha) {
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-3f) return;
    if (area < 0) { float tx = x1, ty = y1; x1 = x2; y1 = y2; x2 = tx; y2 = ty; }
    float ex[3] = { x0, x1, x2 }, ey[3] = { y0, y1, y2 };
    int bx0 = maxi((int)fminf(x0, fminf(x1, x2)) - 1, 0), bx1 = mini((int)fmaxf(x0, fmaxf(x1, x2)) + 2, c->w);
    int by0 = maxi((int)fminf(y0, fminf(y1, y2)) - 1, 0), by1 = mini((int)fmaxf(y0, fmaxf(y1, y2)) + 2, c->h);
    for (int j = by0; j < by1; j++)
        for (int i = bx0; i < bx1; i++) {
            float px = i + 0.5f, py = j + 0.5f, sd = -1e9f;
            for (int k = 0; k < 3; k++) {
                float ax = ex[k], ay = ey[k], bx = ex[(k + 1) % 3], by = ey[(k + 1) % 3];
                float nx = by - ay, ny = -(bx - ax), nl = sqrtf(nx * nx + ny * ny);
                float d = ((px - ax) * nx + (py - ay) * ny) / nl;
                if (d > sd) sd = d;
            }
            float cov = clampf(0.5f - sd, 0, 1);
            if (cov > 0) put(c, i, j, color, cov * alpha);
        }
}

void shadow_rrect(Canvas *c, float x, float y, float w, float h, float r, float blur, float alpha) {
    // Soft falloff that starts inside the shape, so no hard edge shows past the card.
    float cx = x + w / 2, cy = y + h / 2, inner = blur * 0.5f;
    int x0 = maxi((int)(x - blur), 0), x1 = mini((int)(x + w + blur), c->w);
    int y0 = maxi((int)(y - blur), 0), y1 = mini((int)(y + h + blur), c->h);
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++) {
            float sd = sd_rrect(i + 0.5f, j + 0.5f, cx, cy, w / 2, h / 2, r);
            float t = clampf((sd + inner) / (blur + inner), 0, 1);
            float a = (1 - t) * (1 - t) * (1 - t) * alpha;
            if (a > 0.003f) put(c, i, j, 0, a);
        }
}

static const float bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

void paint_glow_background(Canvas *c, uint32_t base, uint32_t c1, float a1, uint32_t c2, float a2) {
    const int S = 8;
    int lw = c->w / S + 2, lh = c->h / S + 2;
    float *lo = malloc((size_t)lw * lh * 3 * sizeof(float));
    float br = (base >> 16) & 255, bg = (base >> 8) & 255, bb = base & 255;
    float r1 = (c1 >> 16) & 255, g1 = (c1 >> 8) & 255, b1 = c1 & 255;
    float r2 = (c2 >> 16) & 255, g2 = (c2 >> 8) & 255, b2 = c2 & 255;
    // Glow 1 bottom-left, glow 2 top-right (matches the VAULT mockup).
    float g1x = c->w * 0.10f, g1y = c->h * 0.92f, g1r = c->w * 0.36f;
    float g2x = c->w * 0.80f, g2y = c->h * 0.28f, g2r = c->w * 0.42f;
    for (int j = 0; j < lh; j++)
        for (int i = 0; i < lw; i++) {
            float px = i * S, py = j * S;
            float d1 = ((px - g1x) * (px - g1x) + (py - g1y) * (py - g1y)) / (g1r * g1r);
            float d2 = ((px - g2x) * (px - g2x) + (py - g2y) * (py - g2y)) / (g2r * g2r);
            float f1 = a1 * expf(-d1 * 2.2f), f2 = a2 * expf(-d2 * 2.2f);
            float *o = lo + (j * lw + i) * 3;
            o[0] = br + (r1 - br) * f1; o[1] = bg + (g1 - bg) * f1; o[2] = bb + (b1 - bb) * f1;
            o[0] += (r2 - o[0]) * f2; o[1] += (g2 - o[1]) * f2; o[2] += (b2 - o[2]) * f2;
        }
    for (int y = 0; y < c->h; y++) {
        float fy = (float)y / S; int jy = (int)fy; float ty = fy - jy;
        for (int x = 0; x < c->w; x++) {
            float fx = (float)x / S; int ix = (int)fx; float tx = fx - ix;
            const float *p00 = lo + (jy * lw + ix) * 3, *p10 = p00 + 3, *p01 = p00 + lw * 3, *p11 = p01 + 3;
            float d = bayer4[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f;
            int ch[3];
            for (int k = 0; k < 3; k++) {
                float v = (p00[k] * (1 - tx) + p10[k] * tx) * (1 - ty) + (p01[k] * (1 - tx) + p11[k] * tx) * ty;
                ch[k] = (int)clampf(v + d, 0, 255);
            }
            c->px[y * c->w + x] = (uint32_t)((ch[0] << 16) | (ch[1] << 8) | ch[2]);
        }
    }
    free(lo);
}

// ---------------------------------------------------------------- images

Image image_decode(const uint8_t *data, int len) {
    Image img = { 0 };
    int w, h, n;
    unsigned char *rgba = stbi_load_from_memory(data, len, &w, &h, &n, 4);
    if (!rgba) return img;
    img.w = w; img.h = h;
    img.px = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        const unsigned char *p = rgba + i * 4;
        img.px[i] = ((uint32_t)p[3] << 24) | (p[0] << 16) | (p[1] << 8) | p[2];
    }
    stbi_image_free(rgba);
    return img;
}

void image_free(Image *img) { free(img->px); img->px = NULL; img->w = img->h = 0; }

static inline uint32_t sample_bilinear(const Image *s, float fx, float fy) {
    fx = clampf(fx, 0, s->w - 1.001f); fy = clampf(fy, 0, s->h - 1.001f);
    int x = (int)fx, y = (int)fy; float tx = fx - x, ty = fy - y;
    int x1 = mini(x + 1, s->w - 1), y1 = mini(y + 1, s->h - 1);
    uint32_t a = s->px[y * s->w + x], b = s->px[y * s->w + x1], c = s->px[y1 * s->w + x], d = s->px[y1 * s->w + x1];
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float va = (a >> sh) & 255, vb = (b >> sh) & 255, vc = (c >> sh) & 255, vd = (d >> sh) & 255;
        float v = (va * (1 - tx) + vb * tx) * (1 - ty) + (vc * (1 - tx) + vd * tx) * ty;
        out |= (uint32_t)(v + 0.5f) << sh;
    }
    return out;
}

Image image_scale_cover(const Image *src, int w, int h) {
    Image out = { w, h, malloc((size_t)w * h * 4) };
    float scale = fmaxf((float)w / src->w, (float)h / src->h);
    float ox = (src->w - w / scale) / 2, oy = (src->h - h / scale) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            out.px[y * w + x] = sample_bilinear(src, ox + (x + 0.5f) / scale - 0.5f, oy + (y + 0.5f) / scale - 0.5f);
    return out;
}

void draw_image_rounded(Canvas *c, const Image *img, int x, int y, float r, float alpha) {
    float cx = x + img->w / 2.0f, cy = y + img->h / 2.0f;
    for (int j = 0; j < img->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= c->h) continue;
        int corner_row = j < r + 1 || j > img->h - r - 2;
        for (int i = 0; i < img->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= c->w) continue;
            uint32_t s = img->px[j * img->w + i];
            float a = ((s >> 24) & 255) / 255.0f * alpha;
            if (corner_row && (i < r + 1 || i > img->w - r - 2))
                a *= clampf(0.5f - sd_rrect(xx + 0.5f, yy + 0.5f, cx, cy, img->w / 2.0f, img->h / 2.0f, r), 0, 1);
            if (a > 0.003f) {
                uint32_t *p = &c->px[yy * c->w + xx];
                *p = blend(*p, s & 0xFFFFFF, a);
            }
        }
    }
}

void image_palette(const Image *img, uint32_t *c1, uint32_t *c2) {
    double acc[2][4] = { { 0 } };
    for (int y = 0; y < img->h; y += 2)
        for (int x = 0; x < img->w; x += 2) {
            uint32_t p = img->px[y * img->w + x];
            double a = ((p >> 24) & 255) / 255.0;
            double r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
            // Weight saturated pixels more so the glow picks the artwork's accent, not its greys.
            double mx = fmax(r, fmax(g, b)), mn = fmin(r, fmin(g, b));
            double w = a * (0.15 + (mx - mn) / 255.0);
            int half = x >= img->w / 2;
            acc[half][0] += r * w; acc[half][1] += g * w; acc[half][2] += b * w; acc[half][3] += w;
        }
    uint32_t out[2];
    for (int k = 0; k < 2; k++) {
        double w = acc[k][3] > 0 ? acc[k][3] : 1;
        double r = acc[k][0] / w, g = acc[k][1] / w, b = acc[k][2] / w;
        double l = (r + g + b) / 3;
        r = l + (r - l) * 1.6; g = l + (g - l) * 1.6; b = l + (b - l) * 1.6;   // boost saturation
        double mx = fmax(r, fmax(g, b));
        if (mx > 1) { double s = 235.0 / mx; r *= s; g *= s; b *= s; }        // normalise brightness
        out[k] = RGB((int)clampf(r, 0, 255), (int)clampf(g, 0, 255), (int)clampf(b, 0, 255));
    }
    *c1 = out[0]; *c2 = out[1];
}

// ---------------------------------------------------------------- text

typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;
    int ok;
} Font;

static Font g_fonts[FONT_COUNT];

typedef struct Glyph {
    int font, size_q, cp;      // key
    int w, h, xoff, yoff;
    unsigned char *bmp;
    struct Glyph *next;
} Glyph;

#define GLYPH_BUCKETS 2048
static Glyph *g_glyphs[GLYPH_BUCKETS];

static unsigned char *read_all(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    if (len) *len = n;
    return buf;
}

int fonts_load(const char *dir) {
    static const char *files[FONT_COUNT] = { "SpaceGrotesk-Bold.ttf", "SpaceGrotesk-Medium.ttf", "JetBrainsMono-SemiBold.ttf" };
    int loaded = 0;
    for (int i = 0; i < FONT_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        g_fonts[i].data = read_all(path, NULL);
        if (g_fonts[i].data && stbtt_InitFont(&g_fonts[i].info, g_fonts[i].data, stbtt_GetFontOffsetForIndex(g_fonts[i].data, 0))) {
            g_fonts[i].ok = 1;
            loaded++;
        }
    }
    return loaded;
}

static const Glyph *get_glyph(int f, float size, int cp) {
    int sq = (int)(size * 4);
    unsigned h = (unsigned)(f * 7919 + sq * 104729 + cp * 31) % GLYPH_BUCKETS;
    for (Glyph *g = g_glyphs[h]; g; g = g->next)
        if (g->font == f && g->size_q == sq && g->cp == cp) return g;
    Glyph *g = calloc(1, sizeof(Glyph));
    g->font = f; g->size_q = sq; g->cp = cp;
    Font *fo = &g_fonts[f];
    if (fo->ok) {
        float scale = stbtt_ScaleForPixelHeight(&fo->info, size);
        g->bmp = stbtt_GetCodepointBitmap(&fo->info, scale, scale, cp, &g->w, &g->h, &g->xoff, &g->yoff);
    }
    g->next = g_glyphs[h];
    g_glyphs[h] = g;
    return g;
}

static int utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    int cp;
    if (p[0] < 0x80) { cp = p[0]; *s += 1; }
    else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); *s += 2; }
    else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); *s += 3; }
    else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); *s += 4; }
    else { cp = '?'; *s += 1; }
    return cp;
}

static float glyph_advance(Font *fo, float scale, int cp, int next) {
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&fo->info, cp, &adv, &lsb);
    float a = adv * scale;
    if (next) a += stbtt_GetCodepointKernAdvance(&fo->info, cp, next) * scale;
    return a;
}

float font_ascent(FontId f, float size) {
    Font *fo = &g_fonts[f];
    if (!fo->ok) return size * 0.8f;
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&fo->info, &asc, &desc, &gap);
    return asc * stbtt_ScaleForPixelHeight(&fo->info, size);
}

static int map_cp(Font *fo, int cp) {
    if (stbtt_FindGlyphIndex(&fo->info, cp)) return cp;
    return 0;   // unsupported (e.g. Japanese titles); skipped
}

float text_width(FontId f, float size, const char *s, float tracking) {
    Font *fo = &g_fonts[f];
    if (!fo->ok) return strlen(s) * size * 0.55f;
    float scale = stbtt_ScaleForPixelHeight(&fo->info, size), w = 0;
    while (*s) {
        int cp = map_cp(fo, utf8_next(&s));
        if (!cp) continue;
        const char *peek = s;
        int nx = *peek ? map_cp(fo, utf8_next(&peek)) : 0;
        w += glyph_advance(fo, scale, cp, nx) + tracking;
    }
    return w;
}

float draw_text(Canvas *c, FontId f, float size, float x, float y, const char *s, uint32_t color, float alpha, float tracking) {
    Font *fo = &g_fonts[f];
    if (!fo->ok) return 0;
    float scale = stbtt_ScaleForPixelHeight(&fo->info, size), x0 = x;
    while (*s) {
        int cp = map_cp(fo, utf8_next(&s));
        if (!cp) continue;
        const char *peek = s;
        int nx = *peek ? map_cp(fo, utf8_next(&peek)) : 0;
        const Glyph *g = get_glyph(f, size, cp);
        if (g->bmp) {
            int gx = (int)floorf(x + 0.5f) + g->xoff, gy = (int)floorf(y + 0.5f) + g->yoff;
            for (int j = 0; j < g->h; j++) {
                int yy = gy + j;
                if (yy < 0 || yy >= c->h) continue;
                for (int i = 0; i < g->w; i++) {
                    int xx = gx + i;
                    if (xx < 0 || xx >= c->w) continue;
                    unsigned char v = g->bmp[j * g->w + i];
                    if (v) {
                        uint32_t *p = &c->px[yy * c->w + xx];
                        *p = blend(*p, color, v / 255.0f * alpha);
                    }
                }
            }
        }
        x += glyph_advance(fo, scale, cp, nx) + tracking;
    }
    return x - x0;
}

void text_ellipsize(FontId f, float size, const char *in, float max_w, char *out, int out_len, float tracking) {
    snprintf(out, out_len, "%s", in);
    if (text_width(f, size, out, tracking) <= max_w) return;
    const char *ell = (g_fonts[f].ok && stbtt_FindGlyphIndex(&g_fonts[f].info, 0x2026)) ? "\xE2\x80\xA6" : "...";
    int n = (int)strlen(out);
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)out[n] & 0xC0) == 0x80) n--;   // stay on a UTF-8 boundary
        while (n > 0 && out[n - 1] == ' ') n--;
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%.*s%s", n, in, ell);
        if (text_width(f, size, tmp, tracking) <= max_w || n == 0) {
            snprintf(out, out_len, "%s", tmp);
            return;
        }
    }
}
