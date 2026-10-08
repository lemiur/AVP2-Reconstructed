"""Snapshot address-bound compiled functions and VC6 .bss ordering for renames.

python tools/d3dren_namecheck.py --module d3dren --out PATH [--compare BASELINE]
Code compares raw bytes, including relocation fields, for every annotated function
including STUBs. Relocation symbol spellings and COFF timestamps are intentionally
excluded. This is stricter than the gate's unchanged aggregate match counts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import modcfg
import build
from coffobj import CoffObj

ROOT=Path(build.ROOT)
sys.path.insert(0,str(ROOT.parent/'nametest/rule'))
from c1hash import layout_key


def snapshot():
    units=build.find_units()
    objects={u.name:CoffObj(u.base_obj) for u in units if Path(u.base_obj).exists()}
    build.bind_symbols(units,objects)
    code={}
    layouts=[]
    errors=[]
    for u in units:
        obj=objects.get(u.name)
        if obj is None:
            errors.append(u.name+': missing object'); continue
        globals_by_name={a.symbol:a.va for a in u.annots if a.kind=='GLOBAL' and a.symbol}
        for a in u.annots:
            if a.kind not in ('FUNCTION','STUB'):continue
            if not a.symbol:
                errors.append(a.where()+': '+str(a.error));continue
            sec,start,end=obj.extent(a.symbol)
            data=sec.data[start:end]
            key=u.name+':%08x'%a.va
            code[key]=dict(size=len(data),sha256=hashlib.sha256(data).hexdigest(),
                           relocation_fields=[[off,typ,addend] for off,s,typ,addend in obj.relocs_in(sec,start,end)])
        for sec in obj.sections:
            if sec.name!='.bss':continue
            known=[]
            for s in sec.syms:
                if s.name not in globals_by_name or s.is_section_symbol or s.cls not in (2,3):continue
                # Same key rule as nametest/rule/check_bss.py.
                match=re.match(r'\?(\w+)@@',s.name)
                keyname=match[1] if match else s.name if s.name.startswith('?') else s.name.lstrip('_')
                known.append(dict(va='%08x'%globals_by_name[s.name],name=keyname,offset=s.value,key=layout_key(keyname)))
            known.sort(key=lambda s:int(s['va'],16))
            if len(known)>1:
                inversions=[(a['va'],b['va']) for a,b in zip(known,known[1:]) if a['key']>b['key']]
                actual_inversions=[(a['va'],b['va']) for a,b in zip(known,known[1:]) if a['offset']>b['offset']]
                layouts.append(dict(unit=u.name,section=sec.index,globals=known,hash_inversions=inversions,
                                    actual_inversions=actual_inversions,consistent=not inversions))
    return dict(schema=1,module=modcfg.NAME,functions=code,bss=layouts,errors=errors)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,required=True);ap.add_argument('--compare',type=Path)
    args=ap.parse_args()
    current=snapshot()
    if args.compare:
        baseline=json.loads(args.compare.read_text())
        allkeys=set(current['functions'])|set(baseline['functions'])
        changes=[k for k in sorted(allkeys) if current['functions'].get(k)!=baseline['functions'].get(k)]
        current['comparison']=dict(baseline=str(args.compare),changed_functions=changes,byte_identical=not changes)
    args.out.write_text(json.dumps(current,indent=1),encoding='utf-8')
    print('functions %d; errors %d; bss %d/%d consistent' % (len(current['functions']),len(current['errors']),sum(r['consistent'] for r in current['bss']),len(current['bss'])))
    if args.compare:
        print('changed compiled functions:',len(changes))
        if changes: print('\n'.join(changes[:20]))
    if current['errors'] or (args.compare and changes):raise SystemExit(1)


if __name__=='__main__': main()
