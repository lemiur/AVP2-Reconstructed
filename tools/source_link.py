r"""Link lithtech.exe from source only: build/release/lithtech.exe.

    python tools/source_link.py [--no-compile] [--map] [--link-flag F ...]
    python tools/build.py release [same options]

Inputs, and nothing else:
  * every engine unit under src/ compiled by build.py's compiler wrapper (stale objects are rebuilt first; a unit that
    does not compile stops the link), in the original's link order (config/units.csv, by address);
  * the prebuilt WONAPI objects (E:\AVP2Source\libs_objs\LT_WONAPI), as in the original build;
  * the stock libraries: VC6 LIBCMT/LIBCPMT/OLDNAMES and the Win32 import libraries, DirectX 8's dinput.lib and
    dxguid.lib;
  * an import library for mss32.dll, generated from the Miles imports our own objects reference (no Miles import
    library is available; the names and stack sizes come from the __stdcall decorations in our objects);
  * config/lithtech.rc (resource.h next to it), compiled with RC; the MFC standard resources it includes come from
    the VC6 MFC include tree.  The two pieces of art no source tree has (the icon IDR_MAINFRAME and the console font
    IDR_CONFONT) are read from build/release/art/, which --art-from-exe fills from the retail image (the only retail
    input, resources only; see config/lithtech.rc);
  * config/icf_aliases.csv: names the original linker folded into an identical function (/OPT:ICF) that our source
    does not define as separate bodies; each becomes a weak external of the function it was folded into.

No byte of the retail image's code or data is read.  LINK runs with /OPT:REF,ICF like the original build.
"""
import argparse
import csv
import os
import re
import struct
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402

ROOT = modcfg.ROOT
CONFIG = os.path.join(ROOT, 'config')
OUT = os.path.join(ROOT, 'build', 'release')
MSVC = r'E:\MSVC6\VC98'
MSDEV_BIN = r'E:\MSVC6\Common\MSDev98\Bin'
LINK = os.path.join(MSVC, 'Bin', 'LINK.EXE')
LIBEXE = os.path.join(MSVC, 'Bin', 'LIB.EXE')
RC = os.path.join(MSDEV_BIN, 'RC.EXE')
WONAPI = os.path.join(modcfg.AVP2, 'libs_objs', 'LT_WONAPI')
DX8_LIB = os.path.join(modcfg.AVP2, 'directx8-msdx8', 'lib')
RETAIL = os.path.join(modcfg.AVP2, 'bin', 'lithtech.exe')
LIBS = ['LIBCMT.LIB', 'LIBCPMT.LIB', 'OLDNAMES.LIB', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib',
        'winmm.lib', 'ole32.lib', 'uuid.lib', 'wsock32.lib',
        os.path.join(DX8_LIB, 'dinput.lib'), os.path.join(DX8_LIB, 'dxguid.lib')]
ART = {'lithtech.ico': ('ICON', 128), 'confont.pcx': (12345, 140)}     # build/release/art/<file>: (resource type, id)


def env():
    e = dict(os.environ)
    e['PATH'] = os.path.join(MSVC, 'Bin') + ';' + MSDEV_BIN + ';' + e.get('PATH', '')
    e['LIB'] = os.path.join(MSVC, 'Lib') + ';' + os.path.join(MSVC, 'MFC', 'Lib')
    e['INCLUDE'] = os.path.join(MSVC, 'Include') + ';' + os.path.join(MSVC, 'MFC', 'Include')
    return e


def run(cmd, **kw):
    r = subprocess.run(cmd, env=env(), capture_output=True, text=True, errors='replace', **kw)
    return r.returncode, (r.stdout + r.stderr).strip()


# ---------------------------------------------------------------- objects

def unit_objects(compile_first=True):
    """The base object of every engine unit, in the original's link order (config/units.csv), compiled if stale."""
    import build
    units = build.find_units()
    if compile_first:
        failed = [u.name for u in units if not build.compile_unit(u, quiet=True)]
        if failed:
            sys.exit('source_link: units do not compile: ' + ', '.join(failed))
    missing = [u.name for u in units if not os.path.exists(u.base_obj)]
    if missing:
        sys.exit('source_link: no object for ' + ', '.join(missing) + ' (run python tools/build.py)')
    with open(os.path.join(CONFIG, 'units.csv'), newline='') as f:
        rank = {r['unit']: i for i, r in enumerate(csv.DictReader(f))}
    ordered = sorted((u for u in units if u.name in rank), key=lambda u: rank[u.name])
    # A unit without code has no config/units.csv row (shared/bdefs): LINK took the objects in file-name order, so it
    # goes before the first ranked unit whose base name sorts after its own.
    for u in sorted((u for u in units if u.name not in rank), key=lambda u: u.name):
        base = os.path.basename(u.name).lower()
        at = next((i for i, o in enumerate(ordered) if os.path.basename(o.name).lower() > base), len(ordered))
        ordered.insert(at, u)
    return [u.base_obj for u in ordered]


# ---------------------------------------------------------------- COFF symbol scan

def coff_symbols(path):
    """(defined, undefined) external symbol names of a COFF object."""
    data = open(path, 'rb').read()
    machine, nsec, _, symptr, nsym = struct.unpack_from('<HHIII', data, 0)
    strtab = symptr + 18 * nsym
    defined, undefined = set(), set()
    i = 0
    while i < nsym:
        o = symptr + 18 * i
        raw = data[o:o + 8]
        value, secno, typ, cls, naux = struct.unpack_from('<IhHBB', data, o + 8)
        if raw[:4] == b'\0\0\0\0':
            off = struct.unpack_from('<I', raw, 4)[0]
            name = data[strtab + off:data.index(b'\0', strtab + off)].decode('latin1')
        else:
            name = raw.rstrip(b'\0').decode('latin1')
        if cls == 2:
            if secno > 0 or (secno == 0 and value):
                defined.add(name)
            elif secno == 0:
                undefined.add(name)
        elif cls == 105:
            defined.add(name)
        i += 1 + naux
    return defined, undefined


# ---------------------------------------------------------------- mss32 import library

def mss32_import_lib(objs):
    """mss32.lib for the Miles imports the objects reference.  mss32.dll exports its __stdcall functions under their
    decorated names (_AIL_startup@0); an import library that imports exactly those names comes from a stub DLL whose
    exports are declared the same way (a .def file would import them undecorated).  The stub's bodies are never run:
    only its import library is used."""
    names = set()
    for p in objs:
        for s in coff_symbols(p)[1]:
            m = re.fullmatch(r'(?:__imp_)?_(AIL_\w+)@(\d+)', s)
            if m:
                names.add((m.group(1), int(m.group(2))))
    d = os.path.join(OUT, 'mss32')
    os.makedirs(d, exist_ok=True)
    src = ['/* generated by tools/source_link.py: the Miles functions lithtech.exe imports, for the import library */']
    for n, size in sorted(names):
        args = ', '.join('int a%d' % k for k in range(size // 4)) or 'void'
        src.append('__declspec(dllexport) void __stdcall %s(%s) {}' % (n, args))
    with open(os.path.join(d, 'mss32_stub.c'), 'w', newline='\n') as f:
        f.write('\n'.join(src) + '\n')
    rc, out = run([os.path.join(MSVC, 'Bin', 'CL.EXE'), '/nologo', '/c', '/Fo' + os.path.join(d, 'mss32_stub.obj'),
                   os.path.join(d, 'mss32_stub.c')])
    if rc:
        sys.exit('source_link: mss32 stub does not compile:\n' + out)
    rc, out = run([LINK, '/nologo', '/DLL', '/NOENTRY', '/NODEFAULTLIB', '/OUT:' + os.path.join(d, 'mss32.dll'),
                   '/IMPLIB:' + os.path.join(d, 'mss32.lib'), os.path.join(d, 'mss32_stub.obj')])
    if rc:
        sys.exit('source_link: mss32 import library:\n' + out)
    return os.path.join(d, 'mss32.lib'), len(names)


# ---------------------------------------------------------------- resources

def extract_art():
    """build/release/art/: the icon group IDR_MAINFRAME as an .ico file and the console font IDR_CONFONT, from the retail
    image's resource section (art only; nothing else of the image is read)."""
    import pefile
    pe = pefile.PE(RETAIL, fast_load=True)
    pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']])
    res = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for e in t.directory.entries:
            for l in e.directory.entries:
                d = l.data.struct
                res[(t.id, e.id)] = pe.get_data(d.OffsetToData, d.Size)
    d = os.path.join(OUT, 'art')
    os.makedirs(d, exist_ok=True)
    grp = res[(14, 128)]                                    # GRPICONDIR + GRPICONDIRENTRY[n] (14 bytes each)
    n = struct.unpack_from('<H', grp, 4)[0]
    entries, images = [], []
    off = 6 + 16 * n
    for k in range(n):
        w, h, cc, r, planes, bits, size, iid = struct.unpack_from('<BBBBHHIH', grp, 6 + 14 * k)
        img = res[(3, iid)]
        entries.append(struct.pack('<BBBBHHII', w, h, cc, r, planes, bits, len(img), off))
        images.append(img)
        off += len(img)
    with open(os.path.join(d, 'lithtech.ico'), 'wb') as f:
        f.write(grp[:6] + b''.join(entries) + b''.join(images))
    with open(os.path.join(d, 'confont.pcx'), 'wb') as f:
        f.write(res[(12345, 140)])


def compile_resources():
    art = os.path.join(OUT, 'art')
    lack = [a for a in ART if not os.path.exists(os.path.join(art, a))]
    if lack:
        sys.exit('source_link: %s missing in %s (python tools/source_link.py --art-from-exe extracts them)' %
                 (', '.join(lack), art))
    res = os.path.join(OUT, 'lithtech.res')
    rc, out = run([RC, '/l', '0x409', '/i', CONFIG, '/i', art, '/i', os.path.join(MSVC, 'MFC', 'Include'),
                   '/d', 'NDEBUG', '/fo', res, os.path.join(CONFIG, 'lithtech.rc')])
    if rc:
        sys.exit('source_link: RC failed:\n' + out)
    return res


# ---------------------------------------------------------------- ICF aliases

def write_alias_obj(path, pairs):
    """A COFF object of weak externals only: each alias resolves to its target (IMAGE_WEAK_EXTERN_SEARCH_ALIAS)."""
    syms, strtab = [], bytearray(4)

    def name_field(n):
        b = n.encode('latin1')
        if len(b) <= 8:
            return b.ljust(8, b'\0')
        off = len(strtab)
        strtab.extend(b + b'\0')
        return struct.pack('<II', 0, off)
    index = {}
    for a, t in pairs:
        if t not in index:
            index[t] = len(syms)
            syms.append(name_field(t) + struct.pack('<IhHBB', 0, 0, 0x20, 2, 0))
    for a, t in pairs:
        syms.append(name_field(a) + struct.pack('<IhHBB', 0, 0, 0x20, 105, 1))
        syms.append(struct.pack('<II', index[t], 3) + bytes(10))
    struct.pack_into('<I', strtab, 0, len(strtab))
    hdr = struct.pack('<HHIIIHH', 0x14c, 0, 0, 20, len(syms), 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + b''.join(syms) + bytes(strtab))


def icf_aliases():
    path = os.path.join(CONFIG, 'icf_aliases.csv')
    if not os.path.exists(path):
        return None, 0
    with open(path, newline='') as f:
        pairs = [(r['name'], r['folded_into']) for r in csv.DictReader(l for l in f if not l.startswith('#'))]
    if not pairs:
        return None, 0
    obj = os.path.join(OUT, 'icf_aliases.obj')
    write_alias_obj(obj, pairs)
    return obj, len(pairs)


# ---------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--no-compile', action='store_true', help='link the existing objects (they must exist)')
    ap.add_argument('--art-from-exe', action='store_true', help='extract the icon and console font from the retail image')
    ap.add_argument('--link-flag', action='append', default=[], help='extra LINK flag (repeatable)')
    a = ap.parse_args(argv)
    if modcfg.NAME != 'lithtech':
        print('source_link.py links the engine; the renderer has tools/source_link_d3dren.py')
        return 2
    os.makedirs(OUT, exist_ok=True)
    exe = os.path.join(OUT, 'lithtech.exe')
    for p in (exe, os.path.join(OUT, 'lithtech.map')):
        if os.path.exists(p):
            os.remove(p)
    objs = unit_objects(not a.no_compile)
    wonapi = sorted(os.path.join(WONAPI, f) for f in os.listdir(WONAPI) if f.lower().endswith('.obj'))
    mss32, nmiles = mss32_import_lib(objs)
    if a.art_from_exe:
        extract_art()
    res = compile_resources()
    alias_obj, nalias = icf_aliases()
    inputs = objs + wonapi + ([alias_obj] if alias_obj else []) + [res] + LIBS + [mss32]
    rsp = os.path.join(OUT, 'link.rsp')
    with open(rsp, 'w') as f:
        f.write('/nologo /NODEFAULTLIB /SUBSYSTEM:WINDOWS,4.0 /MACHINE:IX86 /BASE:0x400000 /FIXED /OPT:REF /OPT:ICF\n')
        f.write('/MAP:"%s" /OUT:"%s"\n' % (os.path.join(OUT, 'lithtech.map'), exe))
        for x in a.link_flag:
            f.write(x + '\n')
        for p in inputs:
            f.write('"%s"\n' % p)
    rc, out = run([LINK, '@' + rsp])
    with open(os.path.join(OUT, 'link.log'), 'w') as f:
        f.write(out + '\n')
    print('%d unit objects, %d WONAPI objects, %d Miles imports, %d ICF aliases' % (len(objs), len(wonapi), nmiles, nalias))
    if out:
        print(out)
    if rc or not os.path.exists(exe):
        print('source_link: LINK failed (rc %d)' % rc)
        return 1
    print('%s: %d bytes' % (exe, os.path.getsize(exe)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
