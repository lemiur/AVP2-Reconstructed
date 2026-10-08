"""Run the existing gen_sync.py against this checkout, without its hard-coded import path.

The generator and fresh Ghidra export live in ../out/ghidra_sync_d3dren.
Only sync_names.csv is written here; SyncNamesPE.java applies it separately.
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
generator=ROOT.parent/'out/ghidra_sync_d3dren/gen_sync.py'
source=generator.read_text(encoding='utf-8')
old="sys.path.insert(0, r'E:\\AVP2Source\\decomp\\tools')"
assert old in source, 'Generator import anchor changed; inspect before executing'
source=source.replace(old,'sys.path.insert(0, '+repr(str(ROOT/'tools'))+')')
exec(compile(source,str(generator),'exec'),dict(__name__='__main__',__file__=str(generator)))
