#!/usr/bin/env python3
"""Capturas de la pantalla "Subir desde el celular" en el build de escritorio.

    make host && python3 tools/make-testdata.py && python3 tests/upload_shots.py

Corre build/host/vault-host con VAULT_SCRIPT=celular y, en paralelo, hace de celular: empareja con
el PIN y sube un juego despacio, para que la captura 35 lo agarre a mitad. Las capturas quedan en
build/host/ (34 a 37).
"""
import http.client
import json
import os
import socket
import subprocess
import sys
import threading
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIN = "731905"
GAME = os.path.join(ROOT, "testdata/data/vault/PSP/GAME/tide_kings.iso")


def call(method, path, body=None, headers=None):
    c = http.client.HTTPConnection("127.0.0.1", 8080, timeout=30)
    h = dict(headers or {})
    if isinstance(body, dict):
        body = json.dumps(body).encode()
        h["Content-Type"] = "application/json"
    c.request(method, path, body=body, headers=h)
    r = c.getresponse()
    d = r.read()
    c.close()
    return r.status, (json.loads(d) if d else None)


def phone(seconds):
    for _ in range(400):
        try:
            socket.create_connection(("127.0.0.1", 8080), timeout=0.2).close()
            break
        except OSError:
            time.sleep(0.02)
    time.sleep(0.6)   # la captura 34 ("esperando") antes de emparejar
    st, r = call("POST", "/api/pair", {"pin": PIN})
    auth = {"Authorization": "Bearer " + r["token"]}
    data = open(GAME, "rb").read()
    st, s = call("POST", "/api/uploads", {"fileName": "tide_kings.iso", "sizeBytes": len(data), "system": "psp"}, auth)
    C = 256 * 1024
    steps = (len(data) + C - 1) // C
    off = s["receivedBytes"]
    while off < len(data):
        part = data[off:off + C]
        st, r = call("PUT", f"/api/uploads/{s['uploadId']}/chunk?offset={off}", part,
                     {**auth, "X-Chunk-CRC32": format(zlib.crc32(part) & 0xFFFFFFFF, "08x")})
        off = r["receivedBytes"]
        time.sleep(seconds / steps)
    st, r = call("POST", f"/api/uploads/{s['uploadId']}/complete", None, auth)
    print("celular: subido", r["game"]["title"])


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 6.0
    # El juego de prueba ya está en testdata para la biblioteca: se sube una copia como juego nuevo.
    dest = os.path.join(ROOT, "testdata/data/vault/games/tide_kings.iso")
    if os.path.exists(dest):
        os.remove(dest)
    env = {**os.environ, "VAULT_SCRIPT": "celular", "SAMPSP4_PIN": PIN, "VAULT_NO_ENGINE": ""}
    env.pop("VAULT_NO_ENGINE")
    app = subprocess.Popen([os.path.join(ROOT, "build/host/vault-host")], cwd=ROOT, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    t = threading.Thread(target=phone, args=(seconds,), daemon=True)
    t.start()
    out, _ = app.communicate(timeout=180)
    t.join(10)
    for line in out.splitlines():
        if "screenshot" in line:
            print(line)


if __name__ == "__main__":
    main()
