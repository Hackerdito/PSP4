// gfx.h - tiny software renderer for a 32-bit XRGB canvas.
#pragma once
#include <stdint.h>

typedef struct {
    int w, h;
    uint32_t *px;   // 0x00RRGGBB
} Canvas;

typedef struct {
    int w, h;
    uint32_t *px;   // 0xAARRGGBB
} Image;

#define RGB(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))
uint32_t hex(uint32_t rgb);
uint32_t mix(uint32_t a, uint32_t b, float t);

Canvas canvas_new(int w, int h);
void canvas_blit(Canvas *dst, const Canvas *src);      // same-size copy

void fill_rect(Canvas *c, int x, int y, int w, int h, uint32_t color, float alpha);
void fill_rrect(Canvas *c, float x, float y, float w, float h, float r, uint32_t color, float alpha);
void stroke_rrect(Canvas *c, float x, float y, float w, float h, float r, float thick, uint32_t color, float alpha);
void fill_circle(Canvas *c, float cx, float cy, float r, uint32_t color, float alpha);
void stroke_circle(Canvas *c, float cx, float cy, float r, float thick, uint32_t color, float alpha);
void draw_line(Canvas *c, float x0, float y0, float x1, float y1, float thick, uint32_t color, float alpha);
void fill_triangle(Canvas *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t color, float alpha);
// Soft drop shadow under a rounded rect.
void shadow_rrect(Canvas *c, float x, float y, float w, float h, float r, float blur, float alpha);

// Background: base colour plus two big soft colour glows (computed at low res, upscaled).
void paint_glow_background(Canvas *c, uint32_t base, uint32_t c1, float a1, uint32_t c2, float a2);

// Images
Image image_decode(const uint8_t *data, int len);   // PNG/JPG via stb_image
Image image_scale_cover(const Image *src, int w, int h);  // fill w*h, crop overflow (bilinear)
void image_free(Image *img);
// Draws an image at (x,y) clipped to a rounded rect of the image's size.
void draw_image_rounded(Canvas *c, const Image *img, int x, int y, float r, float alpha);
// Average colour of the left/right halves (for glows).
void image_palette(const Image *img, uint32_t *c1, uint32_t *c2);

// Text
typedef enum { FONT_DISPLAY = 0, FONT_BODY, FONT_MONO, FONT_COUNT } FontId;
int fonts_load(const char *dir);   // returns number of fonts loaded
float text_width(FontId f, float size, const char *utf8, float tracking);
// Draws text with its baseline at y. Returns advance width.
float draw_text(Canvas *c, FontId f, float size, float x, float y, const char *utf8, uint32_t color, float alpha, float tracking);
// Truncates with "…" to fit max_w; writes into out.
void text_ellipsize(FontId f, float size, const char *in, float max_w, char *out, int out_len, float tracking);
// Font metrics in pixels for a size.
float font_ascent(FontId f, float size);
