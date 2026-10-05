# AVP2 Reconstructed

A byte-matching decompilation of `lithtech.exe` from *Aliens versus Predator 2* (v1.0.9.6): the LithTech 2.2
"Talon" engine, rewritten as C++ that compiles with the original toolchain into the same machine code, function by
function.

## Status

| | Code matched | Functions |
|---|---|---|
| Inventoried function code (objdiff) | 92.19% | 4,558 of 4,774 |
| Engine code | 90.5% | |
| Inventoried lithshared, WONAPI, VC6 CRT functions | 100% | |

- 3,669 annotated functions pass the byte and relocation-consistency checks.
- 41 functions are written but not yet matching (`// STUB:`); most differ only in register allocation,
  instruction scheduling or inlining decisions, and behave the same as the original (checked by a behaviour
  audit).
- 314 of 341 report units are complete. The mixed relink currently reproduces 117 of 119 eligible source units
  byte-for-byte; ftserv and l_allocator still have function-order differences.
- No source stand-ins remain. The default mixed relink retains 21 compiler-generated exception helpers
  (573 bytes) and 21 metadata sections (852 bytes) from six compiled source units. An original-guided check
  verifies their 150 root/helper relocations; the relinker checks actual linked addresses, bytes, and padding.
- The default mixed relink uses 1,304 payload bytes from 38 verified native library sections, including their
  13 relocations and 4 alignment bytes. DirectX contributions match the engine's DX8.1 provenance; the exact
  original UUID archive is unknown, although the installed VC6 UUID objects reproduce the selected data.

These are function-code metrics, not whole-executable completion. The source exception helper ranges occupy
54 existing report entries: 573 payload bytes and 168 padding bytes. Metadata and library-data totals are tracked separately
and do not increase function-code coverage. The mixed relink supplies unfinished regions from the original
executable; it is a layout check, not a standalone rebuilt game.

Live progress: [decomp.dev](https://decomp.dev) (version `lithtech_1.0.9.6`), from the report in
`progress/lithtech_1.0.9.6/report.json`.

## The renderer: `d3d.ren` (module `d3dren`, in progress)

The game draws through a separate renderer DLL, `d3d.ren` (348,160 bytes, linked 2002-02-07). Despite its name it
is the engine's **DirectDraw 7 / Direct3D 7** renderer, not a Direct3D 8 one. The repository decompiles it as a
second module next to the engine, with the same tools: `--module d3dren` (or `DECOMP_MODULE=d3dren`) points the build,
checker, permuter and helpers at `src/d3dren/`, `include/d3dren/`, `config/d3dren/` and `build/d3dren/`
(`tools/modcfg.py` holds everything module-specific). The engine module is the default and is unaffected;
`tools/gate.py` checks that.

The DLL was built with a different compiler from the engine: the VC6 RTM front ends and optimising back end
(12.00.8168, as its Rich header records), not the SP5 + Processor Pack compiler used for `lithtech.exe`.
`tools/vc6cl_d3dren.bat` and `tools/mk_d3dren_toolchain.bat` set that toolchain up.

| d3d.ren | |
|---|---|
| Annotated functions matching | 1,172 of 1,172 (1,170 addresses) |
| Function code matched by source | 137,180 of 280,684 bytes (48.9%) |
| Written but not yet matching (`// STUB:`) | 90 functions (96,326 bytes) |
| Prebuilt library code (VC6 RTM CRT) | 469 functions, 41,550 bytes (14.8%) |
| objdiff | 63.55% of code, 1,638 of 1,729 functions |

The source is organised as the DLL's 52 original object files, recovered from the binary's layout. Work is paused
at the checkpoint above. Still to do: the remaining stubs, the data sections (initialisers, vtables, ownership and
order), removing the last three stand-in definitions, and a relink of the DLL itself. Its string resource and its
three exports are already reconstructed (`config/d3dren/d3dren.rc`, `d3dren.def`) and verified against the
original by `tools/test_d3dren_resources.py`; no complete renderer DLL has been linked yet. The renderer is not
yet part of the decomp.dev report.

## How it works

- **Toolchain.** The original was built with Visual C++ 6.0 SP5 plus the Processor Pack, `/MT /O2`. Every
  function here is compiled with that exact compiler; no newer compiler produces the same code.
- **Annotations.** Each function in `src/` carries a [reccmp](https://github.com/isledecomp/reccmp)-style marker
  with its address in the original binary:
  ```cpp
  // FUNCTION: LITHTECH 0x0044cc80
  void SomeFunction(...)
  ```
  `// STUB:` marks a function that is written but doesn't match yet, `// GLOBAL:` a data symbol.
- **Checking.** `tools/build.py` compiles every unit, compares each annotated function's bytes and relocation
  targets with the original, and reports `MATCH`, `DIFF` or `SIZE`.
- **Progress.** The original binary is cut into per-unit target objects (`tools/mktarget.py`) and compared with
  the compiled objects by [objdiff](https://github.com/encounter/objdiff). Prebuilt library code (the VC6 CRT
  and WONAPI) is identified by `tools/libmatch.py` and counts as matched.
- **Layout.** `tools/relink.py` links the matched objects back into an executable to check that function order,
  data and literals land where the original has them.
- **Reference.** The source follows the structure and names of LithTech's later engine (Jupiter) and the Talon
  SDK where the binary agrees with them.

## Repository layout

```
src/        decompiled engine source, one .cpp per original translation unit (client, server, world, model, ...)
src/d3dren/ decompiled renderer source (d3d.ren), one .cpp per original object file
include/    reconstructed engine headers (include/d3dren/: the renderer's)
config/     unit boundaries, symbol table exported from Ghidra, renames, library matches (config/d3dren/: the renderer's)
tools/      build driver, checker, target-object writer, relinker, behaviour audit, matching aids
progress/   the committed objdiff progress report
```

## Building

This repository contains source code only. It does **not** include the game binary or any of the proprietary
inputs the build needs, and none are provided:

- `lithtech.exe` from your own copy of Aliens versus Predator 2 (v1.0.9.6), and `d3d.ren` for the renderer module;
- Visual C++ 6.0 SP5 with the Processor Pack (engine), and the VC6 RTM compiler passes (renderer);
- the Talon SDK and lithshared headers, and the LithTech Jupiter source used as a reference for some headers;
- the prebuilt VC6 CRT and WONAPI objects (for library matching);
- the DirectX 8.1 `dinput.lib`/`dxguid.lib` and VC6 `uuid.lib` archives named in `config/library_data.json`
  (for native library-data verification);
- the DirectX 8.0 SDK headers (the renderer's DirectDraw 7 / Direct3D 7 headers).

The tools currently expect these at fixed paths on the author's machine (see the constants at the top of
`tools/build.py` and `scripts\vc6cl.bat`). With them in place:

```bash
python tools/build.py              # compile, check, write target objects, objdiff.json and the report
python tools/build.py check <unit> # compile and check one unit
python tools/build.py diff <func>  # side-by-side disassembly against the original
python tools/build.py report       # refresh progress/lithtech_1.0.9.6/report.json
python tools/source_eh.py          # verify source EH helpers against the original; writes build/source_eh.json
python tools/library_data.py       # verify native library data separately from function-code coverage
python -m unittest discover -s tools/tests -v # run verifier regression tests
python tools/build.py --module d3dren          # the same build for the renderer (build/d3dren/)
python tools/gate.py                           # check that the engine module's numbers are unchanged
```

Requires Python 3 with `capstone` and `pefile`, and `objdiff-cli`.

## Legal

This is an independent preservation and research project. It contains no game assets and no part of the original
binary; you need a legally obtained copy of the game to build or compare anything. Aliens
versus Predator 2 and LithTech are trademarks of their respective owners.
