#!/usr/bin/env python3
"""Prueba de punta a punta del servidor de "Subir desde el celular" (build/host/upload-host).

    make upload-host && python3 tests/upload_api_test.py <carpeta con juegos de prueba>

Los juegos de prueba salen de tools/make-testdata.py. El servidor corre en una carpeta temporal
(sus juegos quedan en <tmp>/testdata/data/vault/games) y se reinicia a mitad de una subida para probar
que reanuda.
"""
import hashlib
import http.client
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER = os.path.join(ROOT, "build/host/upload-host")
PIN = "424242"
PORT = 8080
passed = 0


def ok(cond, what):
    global passed
    if not cond:
        print(f"FALLA: {what}")
        sys.exit(1)
    passed += 1
    print(f"  ok  {what}")


def req(method, path, body=None, headers=None, token=None, raw=False):
    c = http.client.HTTPConnection("127.0.0.1", PORT, timeout=20)
    h = dict(headers or {})
    if token:
        h["Authorization"] = f"Bearer {token}"
    if isinstance(body, (dict, list)):
        body = json.dumps(body).encode()
        h["Content-Type"] = "application/json"
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    data = r.read()
    hdrs = dict(r.getheaders())
    c.close()
    if raw:
        return r.status, data, hdrs
    try:
        return r.status, json.loads(data) if data else None
    except ValueError:
        return r.status, data


def start(cwd):
    p = subprocess.Popen([SERVER], cwd=cwd, env={**os.environ, "SAMPSP4_PIN": PIN},
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", PORT), timeout=0.2).close()
            return p
        except OSError:
            time.sleep(0.05)
    p.kill()
    sys.exit("el servidor no arrancó")


def stop(p):
    p.terminate()
    try:
        p.wait(5)
    except subprocess.TimeoutExpired:
        p.kill()


def crc(b):
    return format(zlib.crc32(b) & 0xFFFFFFFF, "08x")


def put_chunk(token, uid, offset, data, crc_hex=None):
    return req("PUT", f"/api/uploads/{uid}/chunk?offset={offset}", body=data,
               headers={"Content-Type": "application/octet-stream", "X-Chunk-CRC32": crc_hex or crc(data)}, token=token)


def upload(token, path, name=None, system="psp", chunk=1 << 20):
    data = open(path, "rb").read()
    name = name or os.path.basename(path)
    st, s = req("POST", "/api/uploads", {"fileName": name, "sizeBytes": len(data), "system": system}, token=token)
    assert st == 200, (st, s)
    off = s["receivedBytes"]
    while off < len(data):
        part = data[off:off + chunk]
        st, r = put_chunk(token, s["uploadId"], off, part)
        assert st == 200, (st, r)
        off = r["receivedBytes"]
    st, r = req("POST", f"/api/uploads/{s['uploadId']}/complete", token=token)
    assert st == 200, (st, r)
    return r["game"]


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    td = sys.argv[1]
    gdir = os.path.join(td, "data/vault/PSP/GAME")
    neon = os.path.join(gdir, "neon_drift.iso")
    iron = os.path.join(gdir, "iron_valley.cso")
    ps1_pbp = os.path.join(gdir, "SLUS00594/EBOOT.PBP")
    cue = os.path.join(td, "usb0/PS1/Midnight Rally (USA).cue")
    bin_ = os.path.join(td, "usb0/PS1/Midnight Rally (USA).bin")

    work = tempfile.mkdtemp(prefix="sampsp4-upload-")
    os.makedirs(os.path.join(work, "build/host"))
    games = os.path.join(work, "testdata/data/vault/games")
    srv = start(work)
    try:
        print("web embebida")
        st, body, h = req("GET", "/", headers={"Accept-Encoding": "gzip"}, raw=True)
        ok(st == 200 and h.get("Content-Encoding") == "gzip", "GET / responde index.html con gzip")
        html = zlib.decompress(body, 16 + zlib.MAX_WBITS).decode()
        ok("SAMPSP4 Upload" in html, "es la web de SAMPSP4 Upload")
        js = html.split('src="./')[1].split('"')[0]
        st, body, h = req("GET", "/" + js, headers={"Accept-Encoding": "gzip"}, raw=True)
        ok(st == 200 and h.get("Content-Type", "").startswith("text/javascript"), f"el JS de la web (/{js})")
        st, body, h = req("GET", "/biblioteca", headers={"Accept-Encoding": "gzip"}, raw=True)
        ok(st == 200 and h.get("Content-Type", "").startswith("text/html"), "una ruta desconocida devuelve la web")

        print("emparejar")
        st, r = req("GET", "/api/games")
        ok(st == 401, "sin token no hay biblioteca (401)")
        st, r = req("POST", "/api/pair", {"pin": "000000"})
        ok(st == 401 and r["error"] == "pin_invalido", "PIN incorrecto -> 401 pin_invalido")
        st, r = req("POST", "/api/pair", {"pin": PIN})
        ok(st == 200 and len(r["token"]) == 32, "PIN correcto -> token")
        tok = r["token"]
        st, r = req("GET", "/api/info", token=tok)
        ok(st == 200 and r["app"] == "SAMPSP4" and r["totalBytes"] > 0 and r["freeBytes"] > 0, f"info: {r['version']}, {r['freeBytes'] >> 30} GB libres")

        print("subida con fallas")
        data = open(neon, "rb").read()
        st, s = req("POST", "/api/uploads", {"fileName": "neon_drift.iso", "sizeBytes": len(data), "system": "psp"}, token=tok)
        ok(st == 200 and s["chunkSize"] == 8 << 20 and s["receivedBytes"] == 0, "crear subida")
        uid = s["uploadId"]
        C = 1 << 20
        st, r = put_chunk(tok, uid, 0, data[:C])
        ok(st == 200 and r["receivedBytes"] == C, "bloque 1")
        st, r = put_chunk(tok, uid, C, data[C:2 * C], crc_hex="deadbeef")
        ok(st == 422 and r["error"] == "crc_incorrecto", "CRC incorrecto -> 422, no se escribe")
        st, r = req("GET", f"/api/uploads/{uid}", token=tok)
        ok(r["receivedBytes"] == C, "tras el 422 sigue en 1 MiB")
        st, r = put_chunk(tok, uid, 3 * C, data[3 * C:4 * C])
        ok(st == 409 and r["error"] == "offset_incorrecto" and r["receivedBytes"] == C, "offset adelantado -> 409 con receivedBytes")

        # Corte a mitad de un bloque: se mandan cabeceras y media carga, y se cierra.
        sk = socket.create_connection(("127.0.0.1", PORT))
        part = data[C:2 * C]
        sk.sendall((f"PUT /api/uploads/{uid}/chunk?offset={C} HTTP/1.1\r\nHost: x\r\nAuthorization: Bearer {tok}\r\n"
                    f"X-Chunk-CRC32: {crc(part)}\r\nContent-Length: {len(part)}\r\n\r\n").encode() + part[:len(part) // 2])
        sk.close()
        time.sleep(0.3)
        st, r = req("GET", f"/api/uploads/{uid}", token=tok)
        ok(r["receivedBytes"] == C, "conexión cortada a medio bloque: no queda basura")

        st, r = put_chunk(tok, uid, C, data[C:2 * C])
        ok(st == 200 and r["receivedBytes"] == 2 * C, "bloque 2 tras el corte")

        print("reiniciar SAMPSP4 a mitad de la subida")
        stop(srv)
        srv = start(work)
        st, r = req("POST", "/api/pair", {"pin": PIN})
        tok = r["token"]
        st, s = req("POST", "/api/uploads", {"fileName": "neon_drift.iso", "sizeBytes": len(data), "system": "psp"}, token=tok)
        ok(st == 200 and s["uploadId"] == uid and s["receivedBytes"] == 2 * C, "la misma subida sigue desde 2 MiB")
        off = s["receivedBytes"]
        while off < len(data):
            st, r = put_chunk(tok, uid, off, data[off:off + C])
            off = r["receivedBytes"]
        st, r = req("POST", f"/api/uploads/{uid}/complete", token=tok)
        g = r["game"]
        ok(st == 200 and g["title"] == "Neon Drift" and g["titleId"] == "ULUS90001" and g["system"] == "psp" and g["format"] == "iso",
           f"completa: {g['title']} {g['titleId']} {g['system']}/{g['format']}")
        ok(sha(os.path.join(games, "neon_drift.iso")) == sha(neon), "el archivo en la PS4 es idéntico al original (SHA-256)")
        ok(not os.listdir(os.path.join(games, ".subidas")), "no quedan temporales")
        st, r = req("POST", "/api/uploads", {"fileName": "neon_drift.iso", "sizeBytes": len(data), "system": "psp"}, token=tok)
        ok(st == 409 and r["error"] == "ya_existe", "volver a subirlo -> 409 ya_existe")

        print("otros formatos")
        g = upload(tok, iron)
        ok(g["format"] == "cso" and g["titleId"] == "ULES90002", f"CSO: {g['title']}")
        g = upload(tok, ps1_pbp, system="ps1")
        ok(g["format"] == "pbp" and g["system"] == "ps1" and os.path.isfile(os.path.join(games, "SLUS00594/EBOOT.PBP")),
           "EBOOT.PBP de PS1 -> games/SLUS00594/EBOOT.PBP")
        upload(tok, bin_, system="ps1")
        g = upload(tok, cue, system="ps1")
        ok(g["format"] == "bincue" and g["system"] == "ps1", f"CUE + BIN: {g['title']}")
        st, r = req("POST", "/api/uploads", {"fileName": "fotos.zip", "sizeBytes": 10, "system": "psp"}, token=tok)
        ok(st == 415, "formato no soportado -> 415")
        st, r = req("POST", "/api/uploads", {"fileName": "../../../system/evil.iso", "sizeBytes": 2048, "system": "psp"}, token=tok)
        ok(st == 200, "un nombre con ../ se acepta solo como nombre de archivo")
        evil = r["uploadId"]
        put_chunk(tok, evil, 0, b"\0" * 2048)
        st, r = req("POST", f"/api/uploads/{evil}/complete", token=tok)
        ok(st == 200 and os.path.isfile(os.path.join(games, "evil.iso")) and not os.path.exists(os.path.join(work, "system")),
           "se guarda como games/evil.iso, nada fuera de la carpeta de juegos")
        req("DELETE", f"/api/games/{r['game']['id']}", token=tok)

        print("biblioteca")
        st, lib = req("GET", "/api/games", token=tok)
        names = sorted(x["title"] for x in lib)
        ok(st == 200 and len([x for x in lib if x["format"] == "bincue"]) == 1, f"juegos: {names}")
        ok(not any(x["title"].endswith(".bin") or "bin" == x["format"] for x in lib), "el .bin no aparece aparte")
        neon_g = next(x for x in lib if x["titleId"] == "ULUS90001")
        st, png, h = req("GET", neon_g["coverUrl"], token=tok, raw=True)
        ok(st == 200 and png[:8] == b"\x89PNG\r\n\x1a\n", "portada ICON0.PNG del ISO")
        cue_g = next(x for x in lib if x["format"] == "bincue")
        st, r = req("GET", cue_g["coverUrl"], token=tok)
        ok(st == 404, "juego sin portada -> 404 (la web muestra la de respaldo)")

        st, r = req("PATCH", f"/api/games/{neon_g['id']}", {"title": "Neon Drift ¡Edición final!"}, token=tok)
        ok(st == 200 and r["title"] == "Neon Drift ¡Edición final!", "renombrar (con acentos)")
        st, lib = req("GET", "/api/games", token=tok)
        ok(any(x["title"] == "Neon Drift ¡Edición final!" for x in lib), "el nombre nuevo queda guardado")

        st, r = req("DELETE", f"/api/games/{cue_g['id']}", token=tok)
        left = os.listdir(games)
        ok(st == 204 and not any(n.startswith("Midnight") for n in left), "borrar un CUE borra también sus .bin")

        print("bloqueo del PIN")
        codes = [req("POST", "/api/pair", {"pin": "111111"})[0] for _ in range(5)]
        st, r = req("POST", "/api/pair", {"pin": PIN})
        ok(codes == [401] * 5 and st == 429, "5 PIN incorrectos bloquean 30 s (429), aunque el siguiente sea correcto")
    finally:
        stop(srv)
        out = srv.stdout.read() if srv.stdout else ""
        log = os.path.join(work, "build/host/upload.log")
        if passed == 0 or "-v" in sys.argv:
            print(out)
        shutil.rmtree(work, ignore_errors=True)
    print(f"\n{passed} comprobaciones OK")


if __name__ == "__main__":
    main()
