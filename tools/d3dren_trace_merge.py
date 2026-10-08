"""Merge independent captures, keeping scenario/frame scopes separate.

python tools/d3dren_trace_merge.py CAPTURE... --out DATABASE
Use one final snapshot per run; never supply overlapping snapshots from one run.
"""
import argparse,hashlib,json
from pathlib import Path

def merge(paths):
 result=dict(schema=1,functions={},scenarios={},installed=[],failures=[],captures=[],entry_methods={})
 hashes=set();installed=set();failures={}
 for index,path in enumerate(paths,1):
  data=json.loads(path.read_text());prefix='capture%d:'%index
  digest=hashlib.sha256(path.read_bytes()).hexdigest()
  if digest in hashes:raise ValueError('Duplicate capture: '+str(path))
  hashes.add(digest)
  result['captures'].append(dict(file=str(path),sha256=digest,pid=data.get('pid'),
   image_sha256=data.get('image_sha256'),frame_count=data.get('frame'),
   scenario_prefix=prefix,module_epochs=data.get('module_epochs'),argument_sampling=data.get('argument_sampling')))
  for tag,row in data.get('scenarios',{}).items():result['scenarios'][prefix+tag]=row
  for va,scenarios in data.get('functions',{}).items():
   result['functions'].setdefault(va,{}).update({prefix+tag:row for tag,row in scenarios.items()})
  installed.update(data.get('installed',[]))
  for va in data.get('installed',[]):
   method=data.get('entry_methods',{}).get(va,'Interceptor')
   methods=result['entry_methods'].setdefault(va,[])
   if method not in methods:methods.append(method)
  for row in data.get('failures',[]):
   key=(row.get('kind'),row.get('va'))
   entry=failures.setdefault(key,dict(row,captures=[],errors=[]))
   if row.get('fallback'):entry['fallback']=row['fallback']
   entry['captures'].append(index)
   if row.get('error') not in entry['errors']:entry['errors'].append(row.get('error'))
 result['installed']=sorted(installed);result['failures']=list(failures.values())
 result['limitations']=['Scenario tags identify attempted actions, not proof of feature activation.',
  'First eight argument samples per entry/scenario; unproven pointer types remain raw.',
  'Frame numbers count Start3D entries, including unsuccessful begin attempts.',
  'Unhit means only unhit in these captures; failed Interceptor entries are separate.',
  'One-shot breakpoint hits record first-hit coverage, with no call-frequency estimate.',
  'Compiler x87 bodies are excluded after an all-entry interception trial caused an exception.',
  'Focus-loss recovery and resolution change had capture/display problems; restoration is not verified.']
 return result

if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('captures',type=Path,nargs='+');ap.add_argument('--out',type=Path,required=True)
 args=ap.parse_args();result=merge(args.captures);args.out.parent.mkdir(parents=True,exist_ok=True)
 args.out.write_text(json.dumps(result,indent=1),encoding='utf-8')
 print(json.dumps(dict(captures=len(result['captures']),functions_observed=len(result['functions']),installed=len(result['installed']),failures=len(result['failures']),scenarios=len(result['scenarios']))))
