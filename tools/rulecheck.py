r"""Code-rule checker: which matched functions also pass the [match] rules (CODE_RULES.md / AGENTS.md "Code rules").

  python tools/rulecheck.py [--module d3dren] [-v] [--unit SUBSTR] [--rule R2]
  python tools/build.py rules [...]                     (the same)

A function counts as matched only when its bytes match (`// FUNCTION:` annotations, which build.py verifies) AND it
passes every [match] rule. One that fails is MATCH-PENDING, with the rules it breaks:

  R1  a type it uses is declared outside a shared header: a struct/class/union/enum defined in a .cpp (at file scope
      or inside a function), or a type defined in more than one header
  R2  a global it uses is declared with `extern` in a .cpp instead of in a header, or declared in headers with
      conflicting types
  R4  its body reads memory through a fixed offset: byte-pointer arithmetic under a cast (`(char *)p + 0x58`), or
      a cast of `this`
  R13 it is a method of a class whose declaration has placeholder virtuals (`virtual void Unk12()`, `pad`, `vf..`)
  R17 its body has fake code: `volatile`, or a statement marked as a probe
  R18 its body is inline assembly (it is verbatim, not matched)

Not blocking, but reported as open work (advisory):
  R7  placeholder names in the function's own name or body (FUN_, DAT_, sub_, local_, param_, UnkType_, m_Unk,
      address-like identifiers)

The checks are textual (comments and string literals are stripped first; bodies are found by brace depth). A
finding that the original's bytes force is recorded at the code as `// RULE-EXCEPTION: R<n>, <reason>` inside the
function or on the line above its annotation; it then no longer blocks.

Writes build/<module>/rulecheck.json. Exit code 0 (the checker reports; the byte gate and the README use its counts).
"""
import argparse
import json
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402   (consumes --module)

ANN = re.compile(r'^\s*//\s*(FUNCTION|STUB):\s*%s\s+0x([0-9a-fA-F]+)(?:\s+(\S+))?' % modcfg.TAG)
EXC = re.compile(r'RULE-EXCEPTION:\s*((?:R\d+[,\s]*)+)')
TYPE_DEF = re.compile(r'^\s*(?:typedef\s+)?(struct|class|union|enum)\s+(\w+)\s*(?::[^{;]*)?\{')
TYPEDEF_ANON = re.compile(r'^\s*\}\s*(\w+)\s*;')
EXTERN = re.compile(r'^\s*extern\s+(?!"C")([^;(]*?)\b(\w+)\s*(\[[^\]]*\])?\s*;')
OFFSET_CAST = re.compile(r'\(\s*(?:const\s+)?(?:char|BYTE|uint8|int8|unsigned\s+char|signed\s+char|LTUINT8)\s*\*\s*\)'
                         r'\s*\(?\s*[\w>.\-\[\]]+\s*\)?\s*\+\s*(?:0x[0-9a-fA-F]+|\d+)')
# a cast of `this` to another class type; `(void *)this` (a context pointer) and casts to a COM interface
# (`reinterpret_cast<IUnknown *>(this)` in QueryInterface) are real boundaries and allowed
THIS_CAST = re.compile(r'\(\s*(?!void\b|const\s+void\b|I[A-Z])[\w\s]+\*+\s*\)\s*this\b'
                       r'|(?:reinterpret|static)_cast\s*<\s*(?!void\b|I[A-Z])[^>]*>\s*\(\s*this\s*\)')
DUMMY_VIRTUAL = re.compile(r'virtual\s+[\w\s\*&]+?\b(?:Unk|unk|pad|Pad|dummy|Dummy|vf|Vf|vfunc|Vfunc)\w*\s*\(')
PLACEHOLDER = re.compile(r'\b(?:FUN_[0-9a-fA-F]+|DAT_[0-9a-fA-F]+|sub_[0-9a-fA-F]+|unk_[0-9a-fA-F]+|local_[0-9a-fA-F]+'
                         r'|param_\d+|UnkType_\w+|m_Unk\w*|[A-Za-z]+_[0-9a-fA-F]{8})\b')


def strip(line):
    """A source line without // comments and string/char literals."""
    line = re.sub(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])*'", '""', line)
    return line.split('//', 1)[0]


def words(text):
    return set(re.findall(r'[A-Za-z_]\w*', text))


class Body:
    def __init__(self, va, kind, name, lines, start, exc):
        self.va, self.kind, self.name, self.lines, self.start = va, kind, name, lines, start
        self.exc = exc          # rules excepted for this function

    @property
    def code(self):
        return '\n'.join(strip(l) for l in _decomment(self.lines))


def _decomment(lines):
    """Lines with /* */ comments removed (line count kept)."""
    out, in_block = [], False
    for raw in lines:
        line = raw
        res = ''
        while True:
            if in_block:
                if '*/' in line:
                    line = line.split('*/', 1)[1]
                    in_block = False
                    continue
                break
            k = strip(line).find('/*')
            if k < 0:
                res += line
                break
            res += line[:k]
            line = line[k + 2:]
            in_block = True
        out.append(res)
    return out


def parse_unit(path):
    """(bodies, unit-local type definitions, unit-local extern declarations).

    Annotations attach to the next definition at any brace depth (methods inside a class body too); stacked
    annotations share it. A definition ends when the brace depth is back at its start on a line with `;` or `}`:
    a function body, or a statement such as a global with a dynamic initialiser (`_$E` annotations)."""
    with open(path, encoding='latin1') as f:
        raw_lines = f.read().splitlines()
    lines = _decomment(raw_lines)
    bodies, types, externs, elsewhere = [], {}, {}, []
    depth = 0
    pending, pend_exc = [], set()
    cur = None                  # [annotations, start depth, lines, start line, exceptions, entered]
    for i, (raw, line) in enumerate(zip(raw_lines, lines)):
        m = ANN.match(raw)
        if m and cur is None:
            pending.append((m.group(1), int(m.group(2), 16), m.group(3)))
            continue
        e = EXC.search(raw)
        c = strip(line)
        if cur is None and e:
            pend_exc |= set(re.findall(r'R\d+', e.group(1)))
        if depth == 0:
            t = TYPE_DEF.match(c)
            if t:
                types.setdefault(t.group(2), i + 1)
            x = EXTERN.match(c)
            if x:
                externs.setdefault(x.group(2), i + 1)
        if cur is None and pending and c.strip() and not raw.lstrip().startswith('//'):
            cur = [pending, depth, [], i + 1, set(pend_exc)]
            pending, pend_exc = [], set()
        if cur is not None:
            cur[2].append(raw)
            if e:
                cur[4] |= set(re.findall(r'R\d+', e.group(1)))
        depth = max(0, depth + c.count('{') - c.count('}'))
        if cur is not None and depth == cur[1] and (';' in c or '}' in c):
            text = ' '.join(strip(x) for x in _decomment(cur[2]))
            head = text.split('{', 1)[0].split('=', 1)[0]
            nm = re.findall(r'([\w:~]+)\s*\(', head) or re.findall(r'([\w:~]+)\s*(?:\[[^\]]*\])?\s*$', head.strip())
            name = nm[-1] if nm else '?'
            base = name.split('::')[-1].lstrip('~')
            for kind, va, mangled in cur[0]:
                if mangled and mangled.startswith('?') and not mangled.startswith('??') and base \
                        and base not in mangled:
                    elsewhere.append((kind, va, mangled))      # names a function defined elsewhere (a header)
                else:
                    bodies.append(Body(va, kind, name, cur[2], cur[3], cur[4]))
            cur = None
    elsewhere.extend(pending)
    return bodies, types, externs, elsewhere


def header_index(inc_dirs, skip):
    """{type name: [files defining it]}, {global name: {declared type text}} over the headers."""
    tdefs, gdecls = {}, {}
    for inc in inc_dirs:
        for d, dirs, files in os.walk(inc):
            dirs[:] = [x for x in dirs if os.path.relpath(os.path.join(d, x), inc).split(os.sep)[0] not in skip]
            for f in files:
                if not f.lower().endswith(('.h', '.hpp', '.inl')):
                    continue
                p = os.path.join(d, f)
                depth = 0
                for line in open(p, encoding='latin1'):
                    c = strip(line)
                    if depth == 0:
                        t = TYPE_DEF.match(c)
                        if t and t.group(1) != 'enum' or (t and t.group(1) == 'enum'):
                            tdefs.setdefault(t.group(2), set()).add(os.path.relpath(p, ROOT)) if t else None
                        x = EXTERN.match(c)
                        if x:
                            gdecls.setdefault(x.group(2), set()).add(' '.join(x.group(1).split()) + (x.group(3) or ''))
                    depth = max(0, depth + c.count('{') - c.count('}'))
    return tdefs, gdecls


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('-v', action='store_true', help='list every MATCH-PENDING function with its findings')
    ap.add_argument('--unit', help='only units whose name contains this')
    ap.add_argument('--rule', help='only report this rule (e.g. R2)')
    ap.add_argument('--quiet', action='store_true', help='summary line only')
    a = ap.parse_args()
    import build as B
    units = B.find_units()
    tdefs, gdecls = header_index([modcfg.INC], set(modcfg.INC_SKIP) if modcfg.NAME == 'lithtech' else {'d3dren'})
    if modcfg.NAME == 'd3dren':     # the renderer's own headers live in include/d3dren and the engine's in include/
        tdefs2, gdecls2 = header_index([os.path.join(modcfg.INC, 'd3dren')], set())
        for k, v in tdefs2.items():
            tdefs.setdefault(k, set()).update(v)
        # a renderer global is its own storage in d3d.ren: an engine global of the same name (g_ScreenWidth) is
        # another binary's variable, so the renderer's declaration replaces it instead of conflicting with it
        gdecls.update(gdecls2)
    dup_types = {k for k, v in tdefs.items() if len(v) > 1}
    conflict_globals = {k for k, v in gdecls.items() if len(v) > 1}

    results = []
    n_elsewhere = [0]
    for u in units:
        if a.unit and a.unit not in u.name:
            continue
        bodies, ltypes, lexterns, elsewhere = parse_unit(u.path)
        n_elsewhere[0] += sum(1 for e in elsewhere if e[0] == 'FUNCTION')
        # types and externs declared inside function bodies count as unit-local too
        for b in bodies:
            for line in b.lines[1:]:
                c = strip(line)
                t = TYPE_DEF.match(c)
                if t:
                    ltypes.setdefault(t.group(2), b.start)
                x = EXTERN.match(c)
                if x:
                    lexterns.setdefault(x.group(2), b.start)
        class_dummy = set()
        for b in bodies:
            pass
        for b in bodies:
            if b.va is None or b.kind != 'FUNCTION':
                continue
            code = b.code
            w = words(code)
            find = {}
            r1 = sorted((w & set(ltypes)) | (w & dup_types))
            if r1:
                find['R1'] = r1
            r2 = sorted((w & set(lexterns)) | (w & conflict_globals))
            if r2:
                find['R2'] = r2
            r4 = [m.group(0) for m in OFFSET_CAST.finditer(code)] + [m.group(0) for m in THIS_CAST.finditer(code)]
            if r4:
                find['R4'] = r4[:3]
            if re.search(r'\bvolatile\b', code) or re.search(r'//\s*probe\b', '\n'.join(b.lines), re.I):
                find['R17'] = ['volatile/probe']
            if re.search(r'\b_*asm\b', code):
                find['R18'] = ['inline assembly body']
            for r in list(find):
                if r in b.exc:
                    find.pop(r)
            adv = sorted(set(PLACEHOLDER.findall(code + ' ' + b.name)))
            results.append({'unit': u.name, 'va': '%08x' % b.va, 'name': b.name, 'line': b.start,
                            'rules': find, 'placeholders': adv[:8], 'n_placeholders': len(adv)})

    # R13: methods of classes with placeholder virtuals (class declarations in headers and units)
    dummy_classes = set()
    for root in [modcfg.INC] + [os.path.dirname(u.path) for u in units[:1]]:
        pass
    for d, _, files in os.walk(modcfg.INC):
        for f in files:
            if f.lower().endswith(('.h', '.hpp')):
                cls = None
                for line in open(os.path.join(d, f), encoding='latin1'):
                    c = strip(line)
                    t = re.match(r'^\s*(?:class|struct)\s+(\w+)', c)
                    if t and '{' in c or (t and not c.rstrip().endswith(';')):
                        cls = t.group(1)
                    if cls and DUMMY_VIRTUAL.search(c):
                        dummy_classes.add(cls)
    for r in results:
        cls = r['name'].split('::')[0] if '::' in r['name'] else None
        if cls in dummy_classes:
            r['rules'].setdefault('R13', [cls])

    pending = [r for r in results if r['rules']]
    if a.rule:
        pending = [r for r in pending if a.rule in r['rules']]
    by_rule = {}
    for r in results:
        for k in r['rules']:
            by_rule[k] = by_rule.get(k, 0) + 1
    named_open = sum(1 for r in results if r['n_placeholders'])
    summary = {
        'module': modcfg.NAME, 'matched': len(results), 'pending': sum(1 for r in results if r['rules']),
        'matched_under_rules': sum(1 for r in results if not r['rules']), 'by_rule': by_rule,
        'advisory_placeholder_functions': named_open, 'body_in_header_unchecked': n_elsewhere[0],
        'duplicate_header_types': sorted(dup_types), 'conflicting_header_globals': sorted(conflict_globals),
        'dummy_virtual_classes': sorted(dummy_classes),
    }
    out = os.path.join(modcfg.BUILD, 'rulecheck.json')
    os.makedirs(modcfg.BUILD, exist_ok=True)
    with open(out, 'w') as f:
        json.dump({'summary': summary, 'functions': results}, f, indent=1)
    print('%s: %d FUNCTION-annotated (byte-matched) functions; %d pass every [match] rule, %d MATCH-PENDING (%s); '
          'advisory: %d with placeholder names; %d annotated functions have their body elsewhere (a header; not checked)' % (
              modcfg.NAME, summary['matched'], summary['matched_under_rules'], summary['pending'],
              ', '.join('%s %d' % kv for kv in sorted(by_rule.items(), key=lambda x: int(x[0][1:]))) or 'none',
              named_open, n_elsewhere[0]))
    if a.quiet:
        return 0
    if dup_types:
        print('types defined in more than one header (R1): %s' % ', '.join(sorted(dup_types)[:30]))
    if conflict_globals:
        print('globals declared with conflicting types in headers (R2): %s' % ', '.join(sorted(conflict_globals)[:30]))
    if dummy_classes:
        print('classes with placeholder virtuals (R13): %s' % ', '.join(sorted(dummy_classes)))
    if a.v or a.unit or a.rule:
        for r in pending:
            print('  %s %-40s %s:%d  %s' % (r['va'], r['name'][:40], r['unit'], r['line'], '; '.join(
                '%s %s' % (k, ', '.join(v[:4])) for k, v in sorted(r['rules'].items()))))
    else:
        per_unit = {}
        for r in pending:
            per_unit[r['unit']] = per_unit.get(r['unit'], 0) + 1
        print('units with the most MATCH-PENDING functions (-v lists every function):')
        for u, n in sorted(per_unit.items(), key=lambda x: -x[1])[:15]:
            print('  %4d  %s' % (n, u))
    return 0


if __name__ == '__main__':
    sys.exit(main())
