// d3d.ren unk/1002d000 (0x1002d000-0x1002d080): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// portal ConVars only (PortalFlow, PortalGraph, PortalsOnly, BlockersOnly).
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// unit unk/1002d000 (0x1002d000-0x1002d080): the portal console variables (PortalFlow, PortalGraph, PortalsOnly,
// BlockersOnly) of a size-optimised object (/O1 /Ob2: constructor called out of line, static initialiser kept apart
// from its jmp wrapper) and a 4-byte `return 0` stub.
// FLAGS: /O1 /Ob2
#include "d3dren/rendererconsolevars.h"

// FUNCTION: D3DREN 0x1002d000 _$E2
// FUNCTION: D3DREN 0x1002d005 _$E1
// GLOBAL: D3DREN 0x10071cc0
ConVar g_CV_PortalFlow("PortalFlow", 0.0f);
// FUNCTION: D3DREN 0x1002d01f _$E5
// FUNCTION: D3DREN 0x1002d024 _$E4
// GLOBAL: D3DREN 0x100718a0
ConVar g_CV_PortalGraph("PortalGraph", 0.0f);
// FUNCTION: D3DREN 0x1002d03e _$E8
// FUNCTION: D3DREN 0x1002d043 _$E7
// GLOBAL: D3DREN 0x10071ce0
ConVar g_CV_PortalsOnly("PortalsOnly", 0.0f);
// FUNCTION: D3DREN 0x1002d05d _$E11
// FUNCTION: D3DREN 0x1002d062 _$E10
// GLOBAL: D3DREN 0x10071880
ConVar g_CV_BlockersOnly("BlockersOnly", 0.0f);

// 4 bytes `xor eax,eax; ret`.  The linker folded (/OPT:ICF) every identical function into this copy: the stub called (with the file name
// argument: `push [argv+4]; call; add esp,4` in 0x1001b8a0)
// by RenderCommand "PortalFile" with a result ignored (d3d_PortalFileCommand() in 0x1001b8a0), and the CRT's
// `int __cdecl _matherr(struct _exception *)` (libraries.json).
// FUNCTION: D3DREN 0x1002d07c
int d3d_PortalFileCommand(char *pFileName)
{
	return 0;
}
