"""Check generated source identifiers and scoped primary Ghidra names."""
import argparse,csv,json,re
from pathlib import Path
from d3dren_rename import COM,STR
ROOT=Path(__file__).resolve().parents[1]
def audit():
 code=[]
 pattern=re.compile(r'\b(?:FUN|DAT|unk)_[A-Za-z0-9_]+')
 for folder,suffix in (('src/d3dren','*.cpp'),('include/d3dren','*.h')):
  for path in (ROOT/folder).rglob(suffix):
   text=STR.sub('""',COM.sub(' ',path.read_text(encoding='latin1')))
   for m in pattern.finditer(text):code.append(dict(file=str(path.relative_to(ROOT)),identifier=m[0]))
 rows=json.loads((ROOT/'config/d3dren/renames_2026-10-08.json').read_text())
 symbols={}
 for r in csv.DictReader((ROOT/'config/d3dren/symbols.csv').open(encoding='utf-8')):
  if r['kind'] in ('func','data','label'):symbols.setdefault(r['addr'],r['name'])
 generated=[];missing=[]
 for r in rows:
  if r.get('classification') in ('source_helper','alias'):continue
  for va in [r.get('va')]+r.get('additional_addresses',[]):
   if not va:continue
   name=symbols.get(va)
   if not name:missing.append(dict(va=va,new=r['new']))
   elif re.match(r'^(?:FUN|DAT|unk)_',name.split('::')[-1]):generated.append(dict(va=va,name=name,new=r['new']))
 all_default_functions=[r for r in csv.DictReader((ROOT/'config/d3dren/symbols.csv').open(encoding='utf-8')) if r['kind']=='func' and re.match(r'^(?:FUN|unk)_',r['name'].split('::')[-1])]
 return dict(generated_code_identifiers=code,scoped_generated_primary_names=generated,scoped_missing_primary_names=missing,all_generated_function_names=all_default_functions)
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path);args=ap.parse_args()
 result=audit();print(json.dumps(result,indent=1))
 if args.out:args.out.write_text(json.dumps(result,indent=1))
 if any(result.values()):raise SystemExit(1)
