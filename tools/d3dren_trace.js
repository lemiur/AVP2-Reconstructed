/* Frida renderer capture. Injected CONFIG is produced by d3dren_trace.py.
 * Raw stack words are recorded even when no argument type is proven.
 * Typed reads are made only for source-declared object/texture/surface pointers.
 */
const rows = {}, failures = [], installed = [], deviceHooks = new Set();
const breakpoints = new Map(), entryMethods = {};
const entryListeners=[],deviceListeners=[];
let moduleEpoch=0;
const reviewedShortEntries = new Set([0x10001410,0x1000dd18,0x100134a0,0x10029ecc,0x1002d07c]);
let scenario = CONFIG.scenario, frame = 0, renderer = null, commands = [];
const seenScenarios = {};
const types = ['normal','model','worldmodel','sprite','light','camera','particle_system','polygrid','line_system','container','canvas'];
function hex(n) { return (n >>> 0).toString(16).padStart(8, '0'); }
function owner(address, functions, base, preferred) {
  const va = address.toUInt32() - base.toUInt32() + preferred;
  let lo = 0, hi = functions.length;
  while (lo < hi) { const mid = (lo + hi) >>> 1; if (functions[mid].va <= va) lo = mid + 1; else hi = mid; }
  const f = functions[lo - 1];
  return f && va < f.end ? { va: hex(f.va), name: f.name, offset: va - f.va, return_address: address.toString() } : null;
}
function caller(address) {
  if (renderer) { const f = owner(address, CONFIG.functions, renderer.base, 0x10000000); if (f) return Object.assign({module:'d3d.ren'},f); }
  const f = owner(address, CONFIG.engine, Process.mainModule.base, 0x400000);
  return f ? Object.assign({module:Process.mainModule.name},f) : {return_address:address.toString(),module:Process.findModuleByAddress(address)?.name || 'unknown'};
}
function row(va) {
  const key = hex(va);
  const set = rows[key] || (rows[key] = {});
  const s = set[scenario] || (set[scenario] = {hits:0,first_frame:frame,last_frame:frame,callers:{},arguments:[],object_types:{},d3d:{}});
  return s;
}
function restoreBreakpoints() {
  for(const site of breakpoints.values()) {
    try { Memory.patchCode(site.address,1,code=>code.writeU8(site.original)); } catch(_) {}
  }
  breakpoints.clear();
}
if(CONFIG.breakpoint_fallback) Process.setExceptionHandler(details=>{
  if(details.type!=='breakpoint') return false;
  const site=breakpoints.get(details.address.toString()) || breakpoints.get(details.context.pc.sub(1).toString());
  if(!site) return false;
  Memory.patchCode(site.address,1,code=>code.writeU8(site.original));
  breakpoints.delete(site.address.toString());
  details.context.pc=site.address;
  const span=seenScenarios[scenario]||(seenScenarios[scenario]={first_frame:frame,last_frame:frame});span.last_frame=frame;
  const s=row(site.function.va);s.hits++;s.last_frame=frame;s.count_kind='first_hit_only';
  const c=caller(details.context.sp.readPointer());s.callers[JSON.stringify(c)]=1;
  send({kind:'breakpoint_hit',va:hex(site.function.va),scenario,frame,caller:c});
  return true;
});
function typed(p, type) {
  if (p.isNull()) return {raw:p.toString(),type:'null'};
  try {
    if (type === 'object') {
      const t = p.add(0xbc).readU8();
      return {raw:p.toString(),type:'LTObject',object_type:t,object_type_name:types[t] || 'invalid'};
    }
    if (type === 'texture') {
      const format=p.add(0x48).readU8();
      const out={raw:p.toString(),type:'RTexture',width:p.add(0x28).readU16(),height:p.add(0x2a).readU16(),format_index:format};
      if(format<7) {
        const record=renderer.base.add(0x62830+4*format).readPointer();
        if(!record.isNull())out.rgb_bit_count=record.add(0x18).readU32();
      }
      return out;
    }
    if (type === 'surface') return {raw:p.toString(),type:'RSurface',width:p.add(0x10).readU32(),height:p.add(0xc).readU32(),rgb_bit_count:p.add(0x58).readU32()};
  } catch (e) { return {raw:p.toString(),type:type,read_error:String(e)}; }
  return {raw:p.toString(),type:'unknown_stack_word'};
}
function hookDevice() {
  if (!renderer) return;
  try {
    const device = renderer.base.add(0x5de30).readPointer();
    if (device.isNull()) return;
    const vt = device.readPointer();
    CONFIG.device_methods.forEach((method,slot) => {
      const address = vt.add(slot * Process.pointerSize).readPointer();
      // A shared implementation may serve multiple slots: avoid falsely labeling it.
      const key = address.toString();
      if (deviceHooks.has(key)) return;
      deviceHooks.add(key);
      const aliases = CONFIG.device_methods.filter((_,j) => vt.add(j*Process.pointerSize).readPointer().equals(address));
      try {
        deviceListeners.push(Interceptor.attach(address, {onEnter(args) {
          if(!renderer) return;
          try {if (!args[0].equals(renderer.base.add(0x5de30).readPointer())) return;} catch(_) {return;}
          const f = owner(this.returnAddress,CONFIG.functions,renderer.base,0x10000000);
          if (!f) return;
          const s = row(parseInt(f.va,16));
          let m = aliases.length===1 ? method : aliases.join('|');
          if (aliases.length===1 && method==='SetRenderState') m += '('+(CONFIG.render_states[args[1].toUInt32()] || args[1].toUInt32())+','+args[2].toUInt32()+')';
          if (aliases.length===1 && method==='SetTextureStageState') m += '('+args[1].toUInt32()+','+(CONFIG.texture_states[args[2].toUInt32()] || args[2].toUInt32())+','+args[3].toUInt32()+')';
          s.d3d[m]=(s.d3d[m]||0)+1;
        }}));
      } catch(e) { failures.push({kind:'device',method,address:key,error:String(e)}); }
    });
  } catch (_) { /* Device does not exist yet. */ }
}
function install(module) {
  if (renderer) return;
  renderer = module;
  moduleEpoch++;
  if (Process.arch!=='ia32') throw new Error('Expected original 32-bit game');
  CONFIG.functions.forEach(f => {
    try {
      const callbacks={onEnter(args) {
        if (f.va===0x1001bd80) { frame++; hookDevice(); }
        const span=seenScenarios[scenario]||(seenScenarios[scenario]={first_frame:frame,last_frame:frame}); span.last_frame=frame;
        const s=row(f.va); s.hits++; s.last_frame=frame;
        const c=caller(this.returnAddress), ck=JSON.stringify(c); s.callers[ck]=(s.callers[ck]||0)+1;
        if (s.arguments.length < 8) {
          const sample={frame,caller:c,ecx:this.context.ecx.toString(),stack_words:[]};
          for (let i=0;i<3;i++) {
            const location=f.arg_locations[i];
            const value=location==='ecx'||location==='edx' ? this.context[location] : args[parseInt(location.slice(5))];
            const v=typed(value,f.arg_types[i]);v.location=location;sample.stack_words.push(v);
            if (v.type==='LTObject') s.object_types[v.object_type_name]=(s.object_types[v.object_type_name]||0)+1;
          }
          s.arguments.push(sample);
        }
      }};
      entryListeners.push(Interceptor.attach(module.base.add(f.va-0x10000000), callbacks));
      if(!installed.includes(hex(f.va))) installed.push(hex(f.va));
      entryMethods[hex(f.va)]='Interceptor';
    } catch(e) {
      const failure={kind:'entry',va:hex(f.va),name:f.name,error:String(e)};failures.push(failure);
      if(CONFIG.breakpoint_fallback && reviewedShortEntries.has(f.va)) {
        try {
          const address=module.base.add(f.va-0x10000000),original=address.readU8();
          const zeroReturn=f.va===0x1002d07c && (address.readU16()===0xc033 || address.readU16()===0xc031) && address.add(2).readU8()===0xc3;
          if(original!==0xc3 && original!==0xc2 && !zeroReturn) throw new Error('Reviewed short entry bytes no longer match RET or xor eax,eax;RET');
          Memory.patchCode(address,1,code=>code.writeU8(0xcc));
          breakpoints.set(address.toString(),{address,original,function:f});
          if(!installed.includes(hex(f.va))) installed.push(hex(f.va));entryMethods[hex(f.va)]='one_shot_breakpoint';failure.fallback='one_shot_breakpoint_installed';
        } catch(b) {failure.fallback_error=String(b);}
      }
    }
  });
  Interceptor.flush();
  send({kind:'installed',module:module.path,base:module.base.toString(),requested:CONFIG.functions.length,installed:installed.length,failures});
}
const observer=Process.attachModuleObserver({onAdded(module) {if(module.name.toLowerCase()==='d3d.ren') install(module);},onRemoved(module) {
  if(!renderer || !module.base.equals(renderer.base)) return;
  renderer=null;
  for(const listener of entryListeners.splice(0)) listener.detach();
  for(const listener of deviceListeners.splice(0)) listener.detach();
  deviceHooks.clear();restoreBreakpoints();
  send({kind:'renderer_unloaded',module_epoch:moduleEpoch,frame});
}});
// Commands run at the start of the engine update, outside renderer/CSHELL draw
// callbacks. Starting a world inside End3D can destroy the active draw context.
Interceptor.attach(Process.mainModule.base.add(0x10db0), {onEnter() {
  if (!commands.length) return;
  const command=commands.shift();
  try {
    const run=new NativeFunction(Process.mainModule.base.add(0x236a0),'void',['pointer'],{abi:'mscdecl',exceptions:CONFIG.breakpoint_fallback?'propagate':'steal'});
    run(Memory.allocUtf8String(command));
    send({kind:'command',scenario,command,frame});
  } catch(e) {send({kind:'command_error',command,error:String(e)});}
}});
setInterval(hookDevice,100);
function snapshot() {
  const functions=JSON.parse(JSON.stringify(rows));
  for (const set of Object.values(functions)) for (const [tag,s] of Object.entries(set)) {
    const span=seenScenarios[tag]; const frames=span ? Math.max(1,span.last_frame-span.first_frame+1) : 0;
    s.scenario_frames=frames; s.calls_per_frame=s.count_kind==='first_hit_only' ? null : frames ? s.hits/frames : null;
    s.callers=Object.entries(s.callers).map(([c,hits])=>Object.assign(JSON.parse(c),{hits}));
  }
  return {functions,scenarios:seenScenarios,frame,installed,failures,entry_methods:entryMethods,module_epochs:moduleEpoch,device_hook_addresses:deviceHooks.size,
          argument_sampling:'First eight calls per function/scenario; only source-proven pointer types are dereferenced'};
}
setInterval(()=>send({kind:'snapshot',data:snapshot()}),2000);
rpc.exports={snapshot,shutdown() {restoreBreakpoints();},tag(name) { scenario=name; seenScenarios[name]={first_frame:frame,last_frame:frame}; return name; },command(command) {commands.push(command);return command;}};
