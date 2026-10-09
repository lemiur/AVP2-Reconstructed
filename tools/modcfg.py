r"""Module selection for the decomp tools.

The repo hosts two matching-decompilation modules that share `include/`:

  lithtech  the engine executable  (src/, config/, build/, objdiff.json at the repo root)   [default]
  d3dren    the D3D8 renderer DLL  (src/d3dren/, config/d3dren/, build/d3dren/)

Select one with `--module <name>` as the first or any argument of tools/build.py (and the tools that call it), or
with the environment variable DECOMP_MODULE.  `--module` is consumed here (removed from sys.argv) and exported to
DECOMP_MODULE so child processes (permute, vtry, hillclimb ...) follow.  With neither, the module is `lithtech`
and every path and flag below is exactly what the tools used before modules existed.

Everything module specific lives in this file: image, source/config/build directories, annotation tag, compiler
wrapper and flags, which `include/` sub-directories belong to the other module, library lists.
"""
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
AVP2 = r'E:\AVP2Source'
DX8_INCLUDE = os.path.join(AVP2, 'directx8-msdx8', 'include')   # read-only reference tree, never copied into the repo


def _take_module_arg():
    argv = sys.argv
    for i, a in enumerate(argv):
        if a == '--module' and i + 1 < len(argv):
            name = argv[i + 1]
            del argv[i:i + 2]
            return name
        if a.startswith('--module='):
            del argv[i]
            return a.split('=', 1)[1]
    return None


NAME = _take_module_arg() or os.environ.get('DECOMP_MODULE') or 'lithtech'
if NAME not in ('lithtech', 'd3dren'):
    sys.exit('unknown module %r (lithtech, d3dren)' % NAME)
os.environ['DECOMP_MODULE'] = NAME

INC = os.path.join(ROOT, 'include')

if NAME == 'lithtech':
    TAG = 'LITHTECH'
    IMAGE = os.path.join(AVP2, 'bin', 'lithtech.exe')
    SRC = os.path.join(ROOT, 'src')
    CONFIG = os.path.join(ROOT, 'config')
    BUILD = os.path.join(ROOT, 'build')
    OBJDIFF_DIR = ROOT
    # the other module's sources / private headers must not be walked by this module's tools
    SRC_SKIP = ('d3dren',)
    INC_SKIP = ('d3dren',)
    CL = os.environ.get('VC6CL') or os.path.join(AVP2, 'scripts', 'vc6cl.bat')
    COMMON_FLAGS = ['/c', '/nologo', '/MT', '/W3', '/DWIN32', '/DNDEBUG', '/I' + INC]
    DEFAULT_OPT = ['/O2']
    CL_ENV = {}
    SHARED_CHECKOUTS = [os.path.join(AVP2, 'decomp')]
    OUT_IMAGE_NAME = 'lithtech.exe'
    REPORT_VERSION = 'lithtech_1.0.9.6'      # decomp.dev version id (progress/<id>/report.json)
else:
    TAG = 'D3DREN'
    IMAGE = os.path.join(AVP2, 'bin', 'talon', 'd3d.ren')
    SRC = os.path.join(ROOT, 'src', 'd3dren')
    CONFIG = os.path.join(ROOT, 'config', 'd3dren')
    BUILD = os.path.join(ROOT, 'build', 'd3dren')
    OBJDIFF_DIR = BUILD
    SRC_SKIP = ()
    INC_SKIP = ()
    # VC6CL is the ENGINE wrapper override (worktrees set it to tools/vc6cl_wt.bat, the Processor Pack compiler); it must
    # not leak into the renderer, or a worktree's d3dren build and gate.py compile nothing.  D3DRENCL overrides this one.
    CL = os.environ.get('D3DRENCL') or os.path.join(TOOLS, 'vc6cl_d3dren.bat')
    # Flags are established in config/d3dren/FACTS.md (evidence: byte-matched functions).  A unit overrides the
    # optimisation part with `// FLAGS: ...` in its first 30 lines, exactly as for lithtech.
    COMMON_FLAGS = ['/c', '/nologo', '/MT', '/W3', '/DWIN32', '/DNDEBUG', '/I' + INC]
    # d3d.ren mixes speed objects (/O2, COMDATs padded to 16) and size objects (/O1, unpadded COMDATs at odd addresses);
    # /Ob2 is needed by some and harmless to all seed units.  Size units say `// FLAGS: /O1 /Ob2` (evidence: FACTS.md 5).
    DEFAULT_OPT = ['/O2', '/Ob2']
    CL_ENV = {'DX8INC': os.environ.get('DX8INC') or DX8_INCLUDE}
    SHARED_CHECKOUTS = [os.path.join(AVP2, 'decomp_d3dren')]
    OUT_IMAGE_NAME = 'd3d.ren'
    REPORT_VERSION = 'd3dren_1.0.9.6'

SYMBOLS_CSV = os.path.join(CONFIG, 'symbols.csv')
RENAMES_CSV = os.path.join(CONFIG, 'renames.csv')
SPLITS_CSV = os.path.join(CONFIG, 'splits.csv')
UNITS_CSV = os.path.join(CONFIG, 'units.csv')
LIBRARIES_JSON = os.path.join(CONFIG, 'libraries.json')
DATA_UNITS_CSV = os.path.join(CONFIG, 'data_units.csv')


def walk(root):
    """os.walk(root) that leaves out the other module's top-level sub-directory (src/d3dren, include/d3dren
    when the module is lithtech)."""
    n = os.path.normcase(root)
    skip = SRC_SKIP if n == os.path.normcase(SRC) else INC_SKIP if n == os.path.normcase(INC) else ()
    for d, dirs, files in os.walk(root):
        if os.path.normcase(d) == n:
            dirs[:] = [x for x in dirs if x not in skip]
        yield d, dirs, files
