#!/usr/bin/env python3
"""Embebe web/dist en src/web_assets.c, comprimido con gzip.

    python3 tools/embed-web.py web/dist src/web_assets.c

La PS4 sirve la web desde el propio eboot: así no depende de leer /app0 después de que
SAMPSP4 sale del sandbox, ni de que el PKG traiga la carpeta en el lugar correcto.
"""
import gzip
import os
import sys

TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".ico": "image/x-icon",
    ".json": "application/json",
    ".woff2": "font/woff2",
    ".txt": "text/plain; charset=utf-8",
}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dist, out = sys.argv[1], sys.argv[2]
    if not os.path.isfile(os.path.join(dist, "index.html")):
        sys.exit(f"no hay index.html en {dist}: compila la web primero (cd web && npm run build)")
    files = []
    for root, _, names in os.walk(dist):
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = "/" + os.path.relpath(full, dist).replace(os.sep, "/")
            ext = os.path.splitext(n)[1].lower()
            if ext not in TYPES:
                continue
            raw = open(full, "rb").read()
            # mtime=0: el mismo dist produce el mismo archivo (builds reproducibles)
            data = gzip.compress(raw, compresslevel=9, mtime=0)
            files.append((rel, TYPES[ext], data, len(raw)))
    files.sort()

    lines = [
        "// Generado por tools/embed-web.py desde web/dist. No editar a mano: make web",
        '#include "web_assets.h"',
        "#include <string.h>",
        "",
    ]
    total_raw = total_gz = 0
    for i, (rel, _, data, raw_len) in enumerate(files):
        total_raw += raw_len
        total_gz += len(data)
        lines.append(f"// {rel}: {raw_len} bytes, {len(data)} con gzip")
        lines.append(f"static const unsigned char k_asset_{i}[{len(data)}] = {{")
        for j in range(0, len(data), 24):
            lines.append("    " + ",".join(str(b) for b in data[j:j + 24]) + ",")
        lines.append("};")
    lines.append("")
    lines.append("static const WebAsset k_assets[] = {")
    for i, (rel, typ, data, _) in enumerate(files):
        lines.append(f'    {{ "{rel}", "{typ}", k_asset_{i}, {len(data)}u, 1 }},')
    lines.append("};")
    lines.append("")
    lines.append("const WebAsset *web_asset_find(const char *path) {")
    lines.append("    for (unsigned i = 0; i < sizeof k_assets / sizeof k_assets[0]; i++)")
    lines.append("        if (strcmp(k_assets[i].path, path) == 0) return &k_assets[i];")
    lines.append("    return 0;")
    lines.append("}")
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(files)} archivos: {total_raw} bytes -> {total_gz} con gzip -> {out}")


if __name__ == "__main__":
    main()
