r"""Cross-module gate: did a change to shared files (include/, tools/) alter the lithtech module?

  python tools/gate.py            # full lithtech `build.py check` in this checkout, compared with config/lithtech_gate.json
  python tools/gate.py --update   # rewrite the baseline from the current numbers (only after an intended lithtech change)
  python tools/gate.py --d3dren   # additionally run the d3dren module check and print its summary

The lithtech baseline is the summary of the unfiltered check at the commit the d3dren module was branched from
(f4a2f65): annotated functions, matching functions, matched bytes, STUB count/bytes, library objects/bytes.  The gate
exits 1 when any number differs (a shared header edit changed lithtech's code generation or the checker), 0 when equal.
It compiles lithtech's units into this checkout's build/ (private to the checkout) and writes no other shared output.
Takes about 80 s.  Run it after every edit of a file in include/ that lithtech units include and before every merge.
"""
import json
import os
import re
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
BASE = os.path.join(ROOT, 'config', 'lithtech_gate.json')


def run(module):
    env = dict(os.environ, DECOMP_LEAD='1', DECOMP_MODULE=module)
    env.pop('DECOMP_AGENT', None)
    r = subprocess.run([sys.executable, os.path.join(TOOLS, 'build.py'), 'check'], cwd=ROOT, env=env,
                       capture_output=True, text=True)
    return r.stdout + r.stderr


def parse(out):
    m = re.search(r'(\d+)/(\d+) annotated functions match; (\d+) of (\d+) \.text function bytes', out)
    s = re.search(r'(\d+) STUBs \((\d+) bytes\)', out)
    lib = re.search(r'libraries: (\d+) prebuilt objects, (\d+) functions, (\d+) code bytes', out)
    cf = 'COMPILE FAILED' in out
    d = {'matching': int(m.group(1)) if m else -1, 'annotated': int(m.group(2)) if m else -1,
         'match_bytes': int(m.group(3)) if m else -1, 'text_function_bytes': int(m.group(4)) if m else -1,
         'stubs': int(s.group(1)) if s else 0, 'stub_bytes': int(s.group(2)) if s else 0,
         'lib_objects': int(lib.group(1)) if lib else 0, 'lib_functions': int(lib.group(2)) if lib else 0,
         'lib_bytes': int(lib.group(3)) if lib else 0}
    return d, cf


def main():
    out = run('lithtech')
    cur, cf = parse(out)
    print('lithtech now     :', json.dumps(cur, sort_keys=True))
    if '--update' in sys.argv:
        json.dump(cur, open(BASE, 'w', newline=''), indent=1, sort_keys=True)
        print('baseline written:', BASE)
        return 0
    base = json.load(open(BASE))
    print('lithtech baseline:', json.dumps(base, sort_keys=True))
    bad = cf or cur != base
    print('GATE %s' % ('FAILED' if bad else 'OK: lithtech unchanged'))
    if cf:
        print('  a lithtech unit failed to compile')
    for k in sorted(base):
        if cur.get(k) != base[k]:
            print('  %s: baseline %s, now %s' % (k, base[k], cur.get(k)))
    if '--d3dren' in sys.argv:
        d = run('d3dren')
        print('--- d3dren check (summary lines) ---')
        print('\n'.join(l for l in d.splitlines() if 'annotated functions match' in l or 'unique matched addresses' in l
                         or 'libraries:' in l or 'COMPILE FAILED' in l))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
