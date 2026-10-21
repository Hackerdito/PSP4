// web_assets.h - la web de "Subir desde el celular", compilada (web/dist) y embebida en el eboot.
// web_assets.c lo genera tools/embed-web.py (make web); no se edita a mano.
#pragma once

typedef struct {
    const char *path;            // "/index.html", "/assets/index-XXXX.js"
    const char *type;            // Content-Type
    const unsigned char *data;
    unsigned len;
    int gzip;                    // data ya va comprimido con gzip
} WebAsset;

const WebAsset *web_asset_find(const char *path);
