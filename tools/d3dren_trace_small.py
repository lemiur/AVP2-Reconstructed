"""Count calls to entries too short for an Interceptor trampoline using Stalker.

python tools/d3dren_trace_small.py --pid PID --seconds 60
Captures only the engine's main render thread. Counts are attributed to the
scenario in control.json at the one-second summary boundary; no arguments or
caller addresses are available. It can run beside the regular capture.
"""
import argparse
import json
from pathlib import Path
import threading
import time
import frida

ROOT=Path(__file__).resolve().parents[1]
RUNTIME=ROOT/'build/d3dren/runtime'
JS=r'''
const rows={}; let tag='small_entry_fallback',followed=null;
const failed=new Set(CONFIG.failed),ren=Process.getModuleByName('d3d.ren');
for(const m of Process.enumerateModules()) if(m.name.toLowerCase()!=='d3d.ren' && !m.base.equals(Process.mainModule.base)) Stalker.exclude({base:m.base,size:m.size});
const listener=Interceptor.attach(ren.base.add(0x1bd80),{onEnter(){
  if(followed!==null)return;followed=this.threadId;
  Stalker.follow(followed,{events:{call:true,ret:false,exec:false,block:false,compile:false},onCallSummary(summary){
    for(const [address,count] of Object.entries(summary)) {
      const va=(ptr(address).toUInt32()-ren.base.toUInt32()+0x10000000).toString(16).padStart(8,'0');
      if(!failed.has(va))continue;
      const set=rows[va]||(rows[va]={});set[tag]=(set[tag]||0)+count;
    }
  }});
  send({kind:'following',thread:followed});
}});
Stalker.queueDrainInterval=1000;
rpc.exports={tag(name){tag=name;},snapshot(){return{rows,thread:followed,tag,summary_interval_ms:1000};},stop(){if(followed!==null)Stalker.unfollow(followed);Stalker.flush();return rows;}};
setInterval(()=>send({kind:'snapshot',data:rpc.exports.snapshot()}),2000);
'''


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--pid',type=int,required=True);ap.add_argument('--seconds',type=int,default=60)
    args=ap.parse_args()
    trace=json.loads((RUNTIME/'trace.json').read_text())
    failed=[r['va'] for r in trace['failures'] if r.get('kind')=='entry']
    session=frida.attach(args.pid);script=session.create_script('const CONFIG='+json.dumps(dict(failed=failed))+';\n'+JS)
    lock=threading.Lock()
    def message(msg,data):
        if msg['type']=='send' and msg['payload']['kind']=='snapshot':
            with lock:(RUNTIME/'small_trace.json').write_text(json.dumps(dict(pid=args.pid,**msg['payload']['data']),indent=1))
        else:print(json.dumps(msg),flush=True)
    script.on('message',message);script.load()
    previous=None;deadline=time.monotonic()+args.seconds
    try:
        while time.monotonic()<deadline:
            control=json.loads((RUNTIME/'control.json').read_text(encoding='utf-8-sig'))
            if control.get('tag')!=previous:
                script.exports_sync.tag(control.get('tag','small_entry_fallback'));previous=control.get('tag')
            time.sleep(.5)
        script.exports_sync.stop()
        message(dict(type='send',payload=dict(kind='snapshot',data=script.exports_sync.snapshot())),None)
    finally:session.detach()


if __name__=='__main__':main()
