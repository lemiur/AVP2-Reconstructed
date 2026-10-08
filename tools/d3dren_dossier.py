"""Build address-keyed renderer evidence dossiers; never edits source or symbols.

python tools/d3dren_dossier.py [--out build/d3dren/dossier] [--trace PATH]
Unknown types, indirect-call receivers and unobserved runtime remain explicitly unknown.
The JSON is the authoritative machine-readable record; Markdown is for review.
"""
import argparse
import bisect
from collections import defaultdict, deque
import csv
import hashlib
import gzip
import json
from pathlib import Path
import re

import capstone
import pefile
import d3dren_names_pending as pending
import d3dren_names_scan as scan
import d3dren_names_convars as convars
import d3dren_names_guids as guids
import d3dren_names_com as com

ROOT = Path(__file__).resolve().parents[1]
GENERATED = re.compile(r'^(?:FUN|DAT|unk|PTR|LAB)_[0-9a-fA-F]+$')
ANN = pending.ANN


def read_csv(path):
    with Path(path).open(encoding='utf-8', newline='') as f:
        return list(csv.DictReader(f))


def source_entries():
    out = defaultdict(list)
    for p in sorted((ROOT / 'src/d3dren').rglob('*.cpp')):
        text = p.read_text(encoding='latin1')
        marks = list(ANN.finditer(text))
        for i, m in enumerate(marks):
            tail = text[m.end():marks[i + 1].start() if i + 1 < len(marks) else len(text)]
            # Keep the annotated definition and its notes, not the next definition's notes.
            out[int(m[2], 16)].append(dict(file=p.relative_to(ROOT).as_posix(),
                line=text.count('\n', 0, m.start()) + 1, annotation=m[1], code=tail.strip()))
    return out


def member_catalog():
    out = defaultdict(list)
    for p in sorted((ROOT / 'include').rglob('*.h')):
        for n, line in enumerate(p.read_text(encoding='latin1').splitlines(), 1):
            m = re.search(r'\b(m_\w+)\s*(?:\[[^;]*?\])?\s*;.*?//\s*(?:offset\s+)?(?:\+)?0x([0-9a-fA-F]+)\b', line)
            if m:
                out[m[1]].append(dict(offset='0x' + m[2].lower(), header=p.relative_to(ROOT).as_posix(), line=n))
    return out


def ghidra_entries():
    p = ROOT.parent / 'out/decomp/d3d_ren_v2.c'
    if not p.exists():
        return {}
    text = p.read_text(encoding='latin1')
    marks = list(re.finditer(r'^//.*?\b(100[0-9a-fA-F]{5})\b.*$', text, re.M))
    return {int(m[1], 16): text[m.end():marks[i + 1].start() if i + 1 < len(marks) else len(text)].strip()
            for i, m in enumerate(marks)}


def build(out, trace=None, renames=None):
    out.mkdir(parents=True, exist_ok=True)
    # The legacy scanner defaults to a different checkout and a shared TEMP cache.
    scan.ROOT = str(ROOT)
    scan.SYMBOLS = str(ROOT / 'config/d3dren/symbols.csv')
    scan.CACHE = str(out / 'scan.pkl')
    obj = scan.build(scan.CACHE)
    cv = convars.extract()
    gd = dict((a, n) for a, n, _ in guids.find())
    defs, _, _ = pending.load()
    bydef = {d['va']: d for d in defs}
    definitions = defaultdict(list)
    for d in defs:
        definitions[d['va']].append(d)
    generated = {d['va'] for d in defs if GENERATED.match((d['name'] or '').split('::')[-1])}
    history = {}
    rename_rows = []
    if renames:
        rename_rows = json.loads(Path(renames).read_text(encoding='utf-8'))
        for r in rename_rows:
            if r.get('classification') in ('source_helper','alias'):
                continue
            for va in [r.get('va')] + r.get('additional_addresses', []):
                if va:
                    history[int(va, 16)] = r
    target = generated | set(history)
    proposals = defaultdict(list)
    for r in read_csv(ROOT / 'config/d3dren/names_proposal.csv'):
        proposals[int(r['address'], 16)].append(r)
    syms = defaultdict(list)
    for r in read_csv(ROOT / 'config/d3dren/symbols.csv'):
        syms[int(r['addr'], 16)].append(r)
    funcs = obj['funcs']
    names = {a: bydef.get(a, {}).get('name') or v[1] for a, v in funcs.items()}
    def label(a):
        return dict(va='%08x' % a, name=names.get(a) or bydef.get(a, {}).get('name') or
                    next((r['name'] for r in syms.get(a, []) if r['kind'] == 'data'), None))
    units = read_csv(ROOT / 'config/d3dren/objects_v2.csv')
    unit_map = {a: next((r for r in units if int(r['start'], 16) <= a < int(r['end'], 16)), None) for a in funcs}
    def unit(a):
        return unit_map.get(a)
    unit_neighbours = defaultdict(list)
    for a, u in unit_map.items():
        if u:
            unit_neighbours[u['unit']].append(a)
    sources, members, gh = source_entries(), member_catalog(), ghidra_entries()
    layouts = com.all_layouts()
    candidates = defaultdict(list)
    for filename in ('jupiter.json', 'twins.json', 'xmatch.json'):
        p = out / filename
        if p.exists():
            for r in json.loads(p.read_text()):
                a = r['va'] if isinstance(r['va'], int) else int(r['va'], 16)
                candidates[a].append(dict(tool=filename, **r))
    anchors = {a for a, n in names.items() if a not in history and n and not GENERATED.match(n.split('::')[-1])
               and not n.startswith(('guess_', 'thunk_FUN_', 'UnkType_'))}
    depths = {a: (0, a) for a in anchors}
    queue = deque(sorted(anchors))
    while queue:
        a = queue.popleft()
        for b in obj['calls'].get(a, []):
            if b not in depths:
                depths[b] = (depths[a][0] + 1, depths[a][1])
                queue.append(b)
    pe = pefile.PE(scan.IMAGE)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    cvaddrs = sorted((r['obj'], r) for r in cv if r['obj'])
    dataaddrs = sorted({a for a, rows in syms.items() if any(r['kind'] == 'data' for r in rows)} | {d['va'] for d in defs if d['kind'] == 'GLOBAL'})
    relocsites = {pe.OPTIONAL_HEADER.ImageBase + e.rva for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', [])
                  for e in block.entries if e.type == 3}
    readers = defaultdict(list)
    fdata = {}
    for a, (end, _) in sorted(funcs.items()):
        refs = defaultdict(list)
        fields, indirect, asm = [], [], []
        for ins in md.disasm(pe.get_data(a - obj['base'], end - a), a):
            asm.append('%08x %s %s' % (ins.address, ins.mnemonic, ins.op_str))
            for op in ins.operands:
                v = None
                if op.type == capstone.x86.X86_OP_MEM:
                    v = op.mem.disp & 0xffffffff
                    if op.mem.base and v < 0x1000 and ins.reg_name(op.mem.base) not in ('esp', 'ebp'):
                        fields.append(dict(at='%08x' % ins.address, register=ins.reg_name(op.mem.base), offset=hex(v)))
                    mode = ''.join(x for bit, x in ((capstone.CS_AC_READ, 'R'), (capstone.CS_AC_WRITE, 'W')) if op.access & bit) or 'unknown'
                elif op.type == capstone.x86.X86_OP_IMM and ins.mnemonic not in ('call', 'jmp') and not ins.mnemonic.startswith('j'):
                    v, mode = op.imm & 0xffffffff, 'address'
                if v is not None and dataaddrs and dataaddrs[0] <= v < obj['base'] + pe.OPTIONAL_HEADER.SizeOfImage:
                    k = bisect.bisect_right(dataaddrs, v) - 1
                    da = dataaddrs[k]
                    # Bounds prevent a reference to a string or section gap from becoming a global-field claim.
                    size = max([int(r['end'], 16) - da for r in syms.get(da, []) if r['kind'] == 'data'] or [4])
                    if v < da + size:
                        rec = dict(at='%08x' % ins.address, target='%08x' % v, offset=v-da, mode=mode,
                                   pe_relocation=any(ins.address <= site < ins.address + ins.size for site in relocsites))
                        refs[da].append(rec)
            if ins.mnemonic == 'call' and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_MEM:
                op = ins.operands[0]
                if op.mem.base and op.mem.disp >= 0 and op.mem.disp % 4 == 0:
                    slot = op.mem.disp // 4
                    indirect.append(dict(at='%08x' % ins.address, slot=slot,
                        candidates=[n + '::' + methods[slot] for n, methods in layouts.items() if slot < len(methods)],
                        resolution='receiver type unknown; slot alone is not an interface identity'))
        for da, accesses in refs.items():
            readers[da].append(dict(**label(a), accesses=accesses))
        fdata[a] = dict(globals=[dict(**label(da), accesses=accesses) for da, accesses in sorted(refs.items())],
                        assembly_fields=fields, indirect_calls=indirect, disassembly='\n'.join(asm))
    runtime = json.loads(gzip.decompress(Path(trace).read_bytes()) if Path(trace).suffix=='.gz' else Path(trace).read_text()) if trace and Path(trace).exists() else {}
    runtime_rows = runtime.get('functions', {})
    records = []
    for a in sorted(set(funcs) | target):
        function = a in funcs
        code = '\n\n'.join(s['code'] for s in sources.get(a, []))
        u = unit(a) if function else None
        neighbours = sorted(b for b in unit_neighbours[u['unit']] if b != a) if u else []
        before, after = [b for b in neighbours if b < a], [b for b in neighbours if b > a]
        strings = []
        get_string = convars.make_lookup(obj['strings'])
        for t in obj['refs'].get(a, {}):
            value = get_string(t)
            if value:
                strings.append(dict(va='%08x' % t, text=value))
        usedcv = []
        for cvaddr, r in cvaddrs:
            hits = [t for t in obj['refs'].get(a, {}) if cvaddr <= t < cvaddr + 0x20 or t in (r['intptr'], r['floatptr'])]
            if hits:
                usedcv.append(dict(name=r['name'], va='%08x' % cvaddr, default=r['default'], refs=['%08x' % t for t in hits]))
        rt = runtime_rows.get('%08x' % a, {})
        observed = sum(s.get('hits', 0) for s in rt.values()) > 0
        failed = next((r for r in runtime.get('failures', []) if r.get('va') == '%08x' % a), None)
        status = 'observed' if observed else 'cold_in_recorded_scenarios' if '%08x' % a in runtime.get('installed', []) else 'instrumentation_failed' if failed else 'not_traced'
        rec = dict(**label(a), kind='function' if function else 'global', pending=a in generated,
            original_generated_scope=a in target, renaming=history.get(a),
            definitions=definitions[a],
            pending_identifiers=sorted({d['name'] for d in definitions[a] if GENERATED.match((d['name'] or '').split('::')[-1])}),
            size=funcs[a][0]-a if function else max([int(r['end'], 16)-a for r in syms.get(a, []) if r['kind']=='data'] or [0]),
            original_object=u, neighbours=[label(b) for b in before[-2:]+after[:2]], proposals=proposals[a],
            symbols=syms[a], sources=sources.get(a, []), callers=[label(b) for b in sorted(obj['callers'].get(a, []))],
            callees=[label(b) for b in sorted(set(obj['calls'].get(a, [])))],
            anchor_depth=depths.get(a, (None, None))[0],
            nearest_anchor=label(depths[a][1]) if a in depths else None,
            strings=strings, convars=usedcv, guids=[dict(va='%08x'%t,name=gd[t]) for t in obj['refs'].get(a,{}) if t in gd],
            source_members=[dict(name=n, layouts=members.get(n, [])) for n in sorted(set(re.findall(r'(?:->|\.)\s*(m_\w+)', code)))],
            d3d_source_calls=sorted(set(re.findall(r'(?:\w+->)?(?:SetRenderState|SetTextureStageState|DrawPrimitive\w*|BeginScene|EndScene|SetTexture|CreateSurface|Blt|Lock|Unlock)\s*\([^;]+', code))),
            candidates=candidates[a], ghidra_decompile=gh.get(a) if not code else None,
            runtime=dict(status=status, scenarios=rt, instrumentation_methods=runtime.get('entry_methods',{}).get('%08x'%a,[]),instrumentation_failure=failed), users=readers.get(a, []) if not function else [],
            **fdata.get(a, {}))
        records.append(rec)
        with (out / ('%08x.md' % a)).open('w', encoding='utf-8', newline='\n') as f:
            f.write('# %08x %s\n\n' % (a, rec['name']))
            for key in ('kind','size','original_object','neighbours','proposals','callers','callees','anchor_depth','nearest_anchor',
                        'strings','convars','guids','globals','users','source_members','assembly_fields','d3d_source_calls','indirect_calls','candidates','runtime','renaming'):
                if key in rec:
                    f.write('## %s\n\n```json\n%s\n```\n\n' % (key,json.dumps(rec[key], indent=2)))
            f.write('## Decompiled source\n\n```cpp\n%s\n```\n\n' % (code or gh.get(a) or 'Unavailable'))
            if function:
                f.write('## Original disassembly\n\n```asm\n%s\n```\n' % rec['disassembly'])
    helpers = [r for r in rename_rows if r.get('classification') in ('source_helper','alias')]
    with (out/'helpers.md').open('w',encoding='utf-8') as f:
        f.write('# Source helpers and macro aliases\n\nThese identifiers do not establish separate native entry points. Address-bearing helper names refer to their owning function.\n\n')
        for r in helpers:
            f.write('## '+r['new']+'\n\n```json\n'+json.dumps(r,indent=2)+'\n```\n\n')
    manifest = dict(schema=1, image=scan.IMAGE, image_sha256=hashlib.sha256(Path(scan.IMAGE).read_bytes()).hexdigest(),
        symbols_sha256=hashlib.sha256(Path(scan.SYMBOLS).read_bytes()).hexdigest(),
        limitations=['Indirect COM slots require a typed receiver; all candidates are kept.',
                      'R/W describes direct memory operands; address-taking does not prove pointee reads/writes.',
                      'Header member offsets are documented offsets, not guessed from untyped register use.',
                      'Unhit is relative to the listed scenarios, not proof that a function is unreachable.'],
        counts=dict(functions=len(funcs), pending_functions=sum(r['pending'] and r['kind']=='function' for r in records),
                    pending_globals=sum(r['pending'] and r['kind']=='global' for r in records),
                    tracked_functions=sum(r['original_generated_scope'] and r['kind']=='function' for r in records),
                    tracked_globals=sum(r['original_generated_scope'] and r['kind']=='global' for r in records)),
        anchor_order=[r['va'] for r in sorted(records,key=lambda r:(r['anchor_depth'] if r['anchor_depth'] is not None else 9999,r['va'])) if r['pending']],
        source_helpers=helpers, records=records)
    (out/'dossier.json').write_text(json.dumps(manifest, indent=1), encoding='utf-8')
    (out/'convars.json').write_text(json.dumps(cv,indent=1),encoding='utf-8')
    print(json.dumps(manifest['counts']))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=ROOT/'build/d3dren/dossier')
    ap.add_argument('--trace',type=Path)
    ap.add_argument('--renames',type=Path,help='retain the original generated-name scope after applying renames')
    args = ap.parse_args()
    build(args.out,args.trace,args.renames)
