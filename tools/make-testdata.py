#!/usr/bin/env python3
"""Builds fake PSP disc images (ISO + CSO) for the desktop test harness.

The games are invented; each image has a real ISO9660 layout with
PSP_GAME/PARAM.SFO and generated artwork, so the scanner exercises the
same code paths it uses for real discs.

usage: tools/make-testdata.py [out_dir=testdata]
"""
import io, os, random, struct, sys, zlib

import pycdlib
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1] if len(sys.argv) > 1 else 'testdata'
INTERNAL = os.path.join(OUT, 'data/vault/PSP/GAME')
USB = os.path.join(OUT, 'usb0/vault/PSP/GAME')

GAMES = [
    # file, title, disc id, palette, art?, pic1?, cso?, usb?
    ('neon_drift.iso',  'Neon Drift',            'ULUS90001', ((61, 245, 200), (27, 108, 255)), True,  True,  False, False),
    ('iron_valley.cso', 'Iron Valley',           'ULES90002', ((255, 122, 61), (255, 210, 61)), True,  True,  True,  False),
    ('echo_run.iso',    'Echo Run',              'ULUS90003', ((185, 168, 255), (255, 95, 162)), True,  False, False, True),
    ('last_signal.cso', 'Last Signal',           'ULJM90004', ((232, 227, 214), (255, 77, 77)),  False, False, True,  False),
    ('tide_kings.iso',  'Tide Kings: Leyendas del Arrecife', 'ULUS90005', ((127, 216, 255), (15, 92, 140)), True, True, False, False),
    ('pixel_siege.iso', 'Pixel Siege',           'NPUH90006', ((200, 255, 90), (47, 168, 102)), True,  False, False, True),
    ('dust_engine.cso', 'Dust Engine',           'ULES90007', ((255, 184, 107), (140, 61, 27)), True,  True,  True,  False),
    ('hoshi_no_uta.iso', '星の歌',   'ULJM90008', ((255, 160, 200), (120, 60, 180)), True,  False, False, False),
]


def make_sfo(entries):
    """entries: list of (key, value) with str (utf8) or int values."""
    entries = sorted(entries)
    keys, data, index = b'', b'', b''
    for k, v in entries:
        if isinstance(v, int):
            raw, fmt, ln, mx = struct.pack('<I', v), 0x0404, 4, 4
        else:
            raw = v.encode('utf-8') + b'\0'
            mx = (len(raw) + 3) & ~3
            fmt, ln = 0x0204, len(raw)
            raw = raw.ljust(mx, b'\0')
        index += struct.pack('<HHIII', len(keys), fmt, ln, mx, len(data))
        keys += k.encode() + b'\0'
        data += raw
    key_off = 20 + len(index)
    keys = keys.ljust((len(keys) + 3) & ~3, b'\0')
    data_off = key_off + len(keys)
    return b'\0PSF' + struct.pack('<IIII', 0x101, key_off, data_off, len(entries)) + index + keys + data


def art(size, c1, c2, seed):
    w, h = size
    rnd = random.Random(seed)
    img = Image.new('RGB', size, c1)
    d = ImageDraw.Draw(img)
    for i in range(6):
        r = rnd.randint(h // 3, h)
        x, y = rnd.randint(-r // 2, w), rnd.randint(-r // 2, h)
        col = tuple(int(a + (b - a) * rnd.random()) for a, b in zip(c1, c2))
        d.ellipse([x - r, y - r, x + r, y + r], fill=col)
    img = img.filter(ImageFilter.GaussianBlur(h / 18))
    d = ImageDraw.Draw(img)
    d.ellipse([w * 0.55, -h * 0.4, w * 0.55 + h * 1.1, h * 0.7], fill=c2)
    d.line([(0, h * 0.8), (w, h * 0.45)], fill=(7, 8, 12), width=max(3, h // 24))
    return img


def png(img):
    b = io.BytesIO()
    img.save(b, 'PNG')
    return b.getvalue()


def build_iso(path, title, disc_id, pal, with_art, with_pic1, seed):
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3)
    iso.add_directory('/PSP_GAME')
    iso.add_directory('/PSP_GAME/SYSDIR')
    sfo = make_sfo([('CATEGORY', 'UG'), ('DISC_ID', disc_id), ('DISC_VERSION', '1.00'),
                    ('PARENTAL_LEVEL', 3), ('PSP_SYSTEM_VER', '6.60'), ('TITLE', title)])
    files = {'/PSP_GAME/PARAM.SFO;1': sfo}
    if with_art:
        files['/PSP_GAME/ICON0.PNG;1'] = png(art((144, 80), pal[0], pal[1], seed))
    if with_pic1:
        files['/PSP_GAME/PIC1.PNG;1'] = png(art((480, 272), pal[1], pal[0], seed + 1))
    # Some bulk so sizes and CSO ratios look realistic.
    rnd = random.Random(seed)
    bulk = bytes(rnd.getrandbits(8) for _ in range(256 * 1024)) + bytes(6 * 1024 * 1024)
    files['/PSP_GAME/SYSDIR/EBOOT.BIN;1'] = bulk
    for name, data in files.items():
        iso.add_fp(io.BytesIO(data), len(data), name)
    iso.write(path)
    iso.close()


def iso_to_cso(src, dst, block=2048):
    data = open(src, 'rb').read()
    n = (len(data) + block - 1) // block
    header = b'CISO' + struct.pack('<IQIBB2x', 24, len(data), block, 1, 0)
    index, blocks, pos = [], [], 24 + (n + 1) * 4
    for i in range(n):
        raw = data[i * block:(i + 1) * block].ljust(block, b'\0')
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        comp = c.compress(raw) + c.flush()
        if len(comp) >= block:
            index.append(pos | 0x80000000); blocks.append(raw); pos += len(raw)
        else:
            index.append(pos); blocks.append(comp); pos += len(comp)
    index.append(pos)
    with open(dst, 'wb') as f:
        f.write(header)
        f.write(b''.join(struct.pack('<I', x) for x in index))
        f.write(b''.join(blocks))


def make_pbp(path, sfo, icon=b'', pic1=b'', psar=b''):
    """EBOOT.PBP: header + 8 entries (SFO, ICON0, ICON1, PIC0, PIC1, SND0, DATA.PSP, DATA.PSAR)."""
    parts = [sfo, icon, b'', b'', pic1, b'', b'~PSP' + b'\0' * 60, psar]
    offs, pos = [], 40
    for p in parts:
        offs.append(pos)
        pos += len(p)
    with open(path, 'wb') as f:
        f.write(b'\0PBP' + struct.pack('<I', 0x10000) + struct.pack('<8I', *offs) + b''.join(parts))


def make_ps1_bin(path, title_id, exe=None):
    """A PS1 disc: ISO9660 with SYSTEM.CNF, stored as raw 2352-byte mode 2 sectors (.bin) + .cue."""
    import pycdlib
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1)
    cnf = ('BOOT = cdrom:\\%s;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFFF0\r\n' % title_id).encode()
    iso.add_fp(io.BytesIO(cnf), len(cnf), '/SYSTEM.CNF;1')
    if exe is None:
        exe = bars_exe()
    iso.add_fp(io.BytesIO(exe), len(exe), '/' + title_id + ';1')
    buf = io.BytesIO()
    iso.write_fp(buf)
    data = buf.getvalue()
    with open(path, 'wb') as f:
        for i in range(0, len(data), 2048):
            m, sec = divmod(i // 2048 + 150, 75 * 60)
            sec, fr = divmod(sec, 75)
            bcd = lambda v: (v // 10) << 4 | v % 10
            f.write(b'\0' + b'\xff' * 10 + b'\0' + bytes([bcd(m), bcd(sec), bcd(fr), 2]) + b'\0' * 8)
            f.write(data[i:i + 2048].ljust(2048, b'\0') + b'\0' * 280)
    name = os.path.basename(path)
    with open(path[:-4] + '.cue', 'w') as f:
        f.write('FILE "%s" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' % name)


def bars_exe():
    """A tiny PS-X EXE that sets a 320x240 screen and fills it with colour bars (GPU fill commands)."""
    T0, T1 = 8, 9
    code = []
    def li(rt, v):
        code.append(0x3C000000 | rt << 16 | (v >> 16))
        code.append(0x34000000 | rt << 21 | rt << 16 | (v & 0xFFFF))
    def put(v, off):
        li(T1, v)
        code.append(0xAC000000 | T0 << 21 | T1 << 16 | off)
    li(T0, 0x1F801810)
    for v in [0x00000000, 0x05000000, 0x06C60260, 0x07040010, 0x08000001, 0x03000000]:
        put(v, 4)                                   # GP1: reset, display area, ranges, 320x240, on
    def fill(rgb, x, y, w, h):
        r, g, b = rgb
        put(0x02000000 | b << 16 | g << 8 | r, 0)  # GP0 fill rectangle
        put(y << 16 | x, 0)
        put(h << 16 | w, 0)
    fill((16, 24, 64), 0, 0, 320, 240)
    fill((63, 123, 255), 40, 40, 80, 150)
    fill((255, 64, 64), 140, 40, 40, 150)
    fill((61, 245, 200), 200, 40, 40, 150)
    fill((240, 240, 240), 40, 200, 240, 16)
    here = 0x80010000 + len(code) * 4
    code += [0x08000000 | ((here >> 2) & 0x3FFFFFF), 0]   # loop forever
    body = b''.join(struct.pack('<I', c) for c in code)
    body = body.ljust((len(body) + 2047) // 2048 * 2048, b'\0')
    h = bytearray(2048)
    h[0:8] = b'PS-X EXE'
    struct.pack_into('<IIII', h, 0x10, 0x80010000, 0, 0x80010000, len(body))
    struct.pack_into('<II', h, 0x30, 0x801FFFF0, 0)
    h[0x4C:0x4C + 55] = b'Sony Computer Entertainment Inc. for North America area'
    return bytes(h) + body


def make_chd(path, tag):
    """Just enough of a CHD v5 for the scanner: header + one metadata entry (CHT2 = CD, 'DVD ' = UMD)."""
    hdr = bytearray(124)
    hdr[0:8] = b'MComprHD'
    hdr[8:12] = struct.pack('>I', 124)
    hdr[12:16] = struct.pack('>I', 5)
    hdr[48:56] = struct.pack('>Q', 124)
    meta = tag + struct.pack('>I', 0x01000010) + struct.pack('>Q', 0) + b'TRACK:1 TYPE:MODE2_RAW'
    with open(path, 'wb') as f:
        f.write(bytes(hdr) + meta + b'\0' * 1024)


def main():
    os.makedirs(INTERNAL, exist_ok=True)
    os.makedirs(USB, exist_ok=True)
    for i, (fn, title, did, pal, a, p1, cso, usb) in enumerate(GAMES):
        out_dir = USB if usb else INTERNAL
        target = os.path.join(out_dir, fn)
        tmp = target + '.tmp.iso' if cso else target
        build_iso(tmp, title, did, pal, a, p1, 1000 + i)
        if cso:
            iso_to_cso(tmp, target)
            os.remove(tmp)
        print(target, os.path.getsize(target))
    # EBOOT.PBP: a PSN PSP game in its own folder, and a PS1 classic.
    d = os.path.join(INTERNAL, 'NPUH10000'); os.makedirs(d, exist_ok=True)
    make_pbp(os.path.join(d, 'EBOOT.PBP'),
             make_sfo([('TITLE', 'Neon Drift Mini'), ('DISC_ID', 'NPUH10000'), ('CATEGORY', 'EG')]),
             png(art((144, 80), (180, 120, 255), (40, 20, 90), 7)), png(art((480, 272), (120, 60, 220), (10, 10, 40), 8)))
    d = os.path.join(INTERNAL, 'SLUS00594'); os.makedirs(d, exist_ok=True)
    make_pbp(os.path.join(d, 'EBOOT.PBP'),
             make_sfo([('TITLE', 'Retro Racer PS1'), ('DISC_ID', 'SLUS00594'), ('CATEGORY', 'ME')]),
             png(art((80, 80), (255, 200, 80), (90, 40, 10), 9)), psar=b'PSISOIMG0000' + b'\0' * 64)
    open(os.path.join(USB, 'PARAM.PBP'), 'wb').write(b'\0PBP' + b'\0' * 36)   # update data: not a game
    make_chd(os.path.join(INTERNAL, 'Shadow Circuit (USA).chd'), b'DVD ')
    # PS1: a .bin/.cue pair, a CD .chd, and a homebrew EBOOT for the EBOOT section.
    ps1 = os.path.join(OUT, 'usb0/PS1'); os.makedirs(ps1, exist_ok=True)
    make_ps1_bin(os.path.join(ps1, 'Midnight Rally (USA).bin'), 'SLUS_012.34')
    make_chd(os.path.join(ps1, 'Crystal Tower (Europe).chd'), b'CHT2')
    d = os.path.join(INTERNAL, 'HOMEBREW_PONG'); os.makedirs(d, exist_ok=True)
    make_pbp(os.path.join(d, 'EBOOT.PBP'), make_sfo([('TITLE', 'Pong Deluxe (homebrew)'), ('CATEGORY', 'MG')]),
             png(art((144, 80), (90, 230, 160), (10, 60, 40), 11)))


if __name__ == '__main__':
    main()
