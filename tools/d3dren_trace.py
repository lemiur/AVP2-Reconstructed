"""Frida capture of the ORIGINAL renderer in an isolated game directory.

python tools/d3dren_trace.py --launch --scenario menu --seconds 600
Write {"tag":"...", "command":"FogEnable 1"} to build/d3dren/runtime/control.json
to tag subsequent hits and queue a command on the game's main update thread.
No on-disk image is patched. --pid attaches without launching a game.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import threading
import time

import frida
import d3dren_names_com as com

ROOT=Path(__file__).resolve().parents[1]
RUNTIME=ROOT/'build/d3dren/runtime'


def enum_names(prefix):
    text=(ROOT.parent/'directx8-msdx8/include/d3dtypes.h').read_text(encoding='latin1')
    return {int(value,0):name for name,value in re.findall(r'\b('+prefix+r'\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)',text)}


def config(scenario, include_crt=False):
    dossier=json.loads((ROOT/'build/d3dren/dossier/dossier.json').read_text(encoding='utf-8'))
    functions=[]
    libraries=json.loads((ROOT/'config/d3dren/libraries.json').read_text())
    library_functions={int(a,16) for u in libraries['units'] for a in u['functions']}
    for r in dossier['records']:
        if r['kind']!='function' or (not include_crt and int(r['va'],16) in library_functions): continue
        code='\n'.join(s['code'] for s in r['sources'])
        signature=next((l.strip() for l in code.splitlines() if l.strip() and not l.strip().startswith('//')), '')
        m=re.search(r'\(([^()]*)\)',signature)
        params=m[1].split(',') if m else []
        types=[]
        for param in params[:3]:
            if re.search(r'\b(?:LTObject|ModelInstance|SpriteInstance|LTPolyGrid|ParticleSystem|LineSystem|LTCanvas|DynamicLight)\s*\*',param): types.append('object')
            elif re.search(r'\bRTexture\s*\*',param): types.append('texture')
            elif re.search(r'\bRSurface\s*\*',param): types.append('surface')
            else: types.append(None)
        locations=['ecx','edx','stack0'] if '__fastcall' in signature else ['stack0','stack1','stack2']
        functions.append(dict(va=int(r['va'],16),end=int(r['va'],16)+r['size'],name=r['name'],
                              arg_types=(types+[None]*3)[:3],arg_locations=locations))
    with (ROOT/'config/symbols.csv').open(encoding='utf-8') as f:
        engine=[dict(va=int(r['addr'],16),end=int(r['end'],16),name=r['name']) for r in csv.DictReader(f) if r['kind']=='func']
    from coffobj import undecorate
    proven=json.loads((ROOT/'build/namemap.json').read_text())
    for f in engine:
        if '%08x'%f['va'] in proven:f['name']=undecorate(proven['%08x'%f['va']])
    return dict(scenario=scenario,include_crt=include_crt,excluded_library_functions=len(library_functions) if not include_crt else 0,
                functions=sorted(functions,key=lambda r:r['va']),engine=sorted(engine,key=lambda r:r['va']),
                device_methods=com.all_layouts()['IDirect3DDevice7'],render_states=enum_names('D3DRENDERSTATE_'),texture_states=enum_names('D3DTSS_'))


def prepare_game():
    game=RUNTIME/'game'
    game.mkdir(parents=True,exist_ok=True)
    reference=ROOT.parent/'run_decomp'
    for p in reference.iterdir():
        if p.is_file() and (p.suffix.lower() in ('.dll','.cfg') or p.name in ('dgVoodoo.conf','AVP2DLL.REZ')):
            if not (game/p.name).exists(): shutil.copy2(p,game/p.name)
    for name in ('Save','Profiles'):
        if not (game/name).exists(): shutil.copytree(reference/name,game/name)
    shutil.copy2(ROOT.parent/'bin/lithtech.exe',game/'lithtech.exe')
    shutil.copy2(ROOT.parent/'bin/talon/d3d.ren',game/'d3d.ren')
    # Windowed capture keeps the task UI accessible and avoids focus-loss fullscreen churn.
    cfg=game/'autoexec.cfg'
    text=cfg.read_text(encoding='latin1')
    text+='\n"Windowed" "1"\n"ScreenWidth" "640"\n"ScreenHeight" "480"\n"DisableMovies" "1"\n'
    cfg.write_text(text,encoding='latin1')
    install=Path(r'C:\Program Files (x86)\Aliens vs. Predator 2')
    argv=[str(game/'lithtech.exe')]
    for name in ('SOUNDS','AVP2','AVP2DLL','ALIEN','MARINE','PREDATOR','MULTI','AVP2P1','AVP2L'):
        argv+=['-rez',str((game if name=='AVP2DLL' else install)/(name+'.REZ'))]
    return game,argv


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    group=ap.add_mutually_exclusive_group(required=True)
    group.add_argument('--launch',action='store_true');group.add_argument('--pid',type=int)
    ap.add_argument('--scenario',default='menu');ap.add_argument('--seconds',type=int,default=600)
    ap.add_argument('--include-crt',action='store_true',help='include compiler-runtime functions; unsafe for x87 register-based entry points')
    ap.add_argument('--breakpoint-fallback',action='store_true',help='one-shot INT3 fallback for the five reviewed short renderer entries')
    ap.add_argument('--sequence',type=Path,help='JSON array of {at:seconds,tag,command} scripted scenarios')
    ap.add_argument('--stop',action='store_true',help='stop ONLY the process launched by this capture at the end')
    args=ap.parse_args()
    RUNTIME.mkdir(parents=True,exist_ok=True)
    cfg=config(args.scenario,args.include_crt)
    cfg['breakpoint_fallback']=args.breakpoint_fallback
    (RUNTIME/'trace_config.json').write_text(json.dumps(cfg),encoding='utf-8')
    device=frida.get_local_device()
    pid=args.pid
    if args.launch:
        cwd,argv=prepare_game()
        pid=device.spawn(argv,cwd=str(cwd))
    session=device.attach(pid)
    script=session.create_script('const CONFIG='+json.dumps(cfg)+';\n'+(ROOT/'tools/d3dren_trace.js').read_text())
    ended=threading.Event()
    write_lock=threading.Lock()
    def message(msg,data):
        if not (msg['type']=='send' and msg['payload'].get('kind')=='snapshot'):
            with (RUNTIME/'events.jsonl').open('a',encoding='utf-8') as f: f.write(json.dumps(msg)+'\n')
        if msg['type']=='send':
            payload=msg['payload']
            if payload['kind']=='snapshot':
                record=dict(schema=1,pid=pid,frida=frida.__version__,image_sha256=hashlib.sha256((ROOT.parent/'bin/talon/d3d.ren').read_bytes()).hexdigest(),**payload['data'])
                with write_lock:
                    temp=RUNTIME/'trace.tmp';temp.write_text(json.dumps(record,indent=1),encoding='utf-8')
                    for retry in range(10):
                        try:
                            temp.replace(RUNTIME/'trace.json');break
                        except PermissionError:
                            if retry==9:raise
                            time.sleep(0.02)
            else: print(json.dumps(payload),flush=True)
        else: print(json.dumps(msg),flush=True)
    script.on('message',message)
    session.on('detached',lambda *a: ended.set())
    try:
        script.load()
        (RUNTIME/'pid.json').write_text(json.dumps(dict(pid=pid,launched=args.launch)),encoding='utf-8')
        print('capture pid %d'%pid,flush=True)
        if args.launch: device.resume(pid)
        deadline=time.monotonic()+args.seconds
        started=time.monotonic()
        sequence=sorted(json.loads(args.sequence.read_text()),key=lambda r:r['at']) if args.sequence else []
        previous=None
        while not ended.is_set() and time.monotonic()<deadline:
            while sequence and time.monotonic()-started>=sequence[0]['at']:
                command=sequence.pop(0)
                if command.get('tag'): script.exports_sync.tag(command['tag'])
                if command.get('command'): script.exports_sync.command(command['command'])
            control=RUNTIME/'control.json'
            if control.exists():
                content=control.read_text(encoding='utf-8-sig')
                if content!=previous:
                    command=json.loads(content)
                    if command.get('tag'): script.exports_sync.tag(command['tag'])
                    if command.get('command'): script.exports_sync.command(command['command'])
                    previous=content
            ended.wait(0.5)
        if not ended.is_set(): message(dict(type='send',payload=dict(kind='snapshot',data=script.exports_sync.snapshot())),None)
    finally:
        if not ended.is_set():
            try: script.exports_sync.shutdown()
            except frida.InvalidOperationError: pass
        session.detach()
        if args.launch and args.stop:
            try: device.kill(pid)
            except frida.ProcessNotFoundError: pass


if __name__=='__main__': main()
