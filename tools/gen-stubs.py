#!/usr/bin/env python3
"""Generate PS4 stub shared libraries (.so) from ps4libdoc JSON files.

Each stub exports the symbol *names* of a system library; create-fself turns the
names into NIDs and imports them from the real module at runtime.

usage: gen-stubs.py <libdoc_lib_dir> <out_dir> <sprx> [<sprx> ...]
"""
import json, os, re, subprocess, sys, tempfile

VALID = re.compile(r'^[A-Za-z_][A-Za-z0-9_$.@]*$')

def main():
    src, out, sprxs = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    libs = {}
    for sprx in sprxs:
        with open(os.path.join(src, sprx + '.sprx.json'), encoding='utf-8-sig') as f:
            doc = json.load(f)
        for mod in doc['modules']:
            for lib in mod['libraries']:
                for s in lib['symbols']:
                    n = s.get('name')
                    if n and VALID.match(n):
                        libs.setdefault(lib['name'], set()).add(n)
    # A symbol exported by several libraries is imported from the first that has it.
    # Keep "libkernel" authoritative so musl's POSIX calls bind there.
    seen = set(libs.get('libkernel', ()))
    for name in sorted(libs):
        if name != 'libkernel':
            libs[name] = libs[name] - seen
    tmp = tempfile.mkdtemp()
    for name, syms in sorted(libs.items()):
        if not syms:
            continue
        asm = os.path.join(tmp, name + '.S')
        with open(asm, 'w') as f:
            f.write('.text\n')
            for s in sorted(syms):
                f.write(f'.global "{s}"\n.type "{s}", @function\n"{s}":\n\tret\n')
        so = os.path.join(out, name + '.so')
        subprocess.check_call(['clang', '--target=x86_64-pc-freebsd12-elf', '-fPIC', '-shared',
                               '-nostdlib', '-fuse-ld=lld', f'-Wl,-soname,{name}.so', asm, '-o', so])
        print(f'{name}.so: {len(syms)} symbols')

if __name__ == '__main__':
    main()
