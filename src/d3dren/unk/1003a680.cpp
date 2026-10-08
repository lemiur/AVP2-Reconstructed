// d3d.ren unk/1003a680 (0x1003a680-0x1003b502): P (packed COMDATs) object of config/d3dren/objects_v2.csv.
// vertex-buffer pool base class + cache; Jupiter VertexBufferController is the only candidate name.
//
// Notes carried over from the merged unit this object was split from (statements about the whole merged file, its `_$E` numbers and its flags are
// historical; the object's own flags are the FLAGS line below):
// d3d.ren unit unk/1003a680 (0x1003a680-0x1003b502): the model vertex buffer pool base class (vbpool.h), the vertex buffer
// cache derived from it with its static instance 0x10093b10, and the CMoArray template code both need (CMoArray of
// IDirect3DVertexBuffer7 pointers, vtable 0x1004653c; CMoArray of the 16-byte cache entries, vtable 0x1004656c).
// Size object (unpadded COMDATs at odd addresses): /O1 /Ob2 (/Ob2 merges the compiler generated static initialiser
// wrappers of the cache object, like in unk/10001000; without it they stay four functions).
// FLAGS: /O1 /Ob2
#include <string.h>
#include "d3dren/vbpool.h"
#include "d3dren/vbcache.h"

// guess: inline wrappers of the two vertex buffer draw calls (never out of line in the exe).  Evidence: every call site
// evaluates the vertex buffer, start vertex and count into registers in argument order before the call, as an inlined
// function with by-value parameters does and a direct call through the device does not.  The names are invented.
static inline HRESULT UnkInline_DrawPrimitiveVB(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER7 pVB,
	DWORD dwStartVertex, DWORD dwNumVertices, DWORD dwFlags)
{
	return pDevice->DrawPrimitiveVB(type, pVB, dwStartVertex, dwNumVertices, dwFlags);
}

static inline HRESULT UnkInline_DrawIndexedPrimitiveVB(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, LPDIRECT3DVERTEXBUFFER7 pVB,
	DWORD dwStartVertex, DWORD dwNumVertices, LPWORD pwIndices, DWORD dwIndexCount, DWORD dwFlags)
{
	return pDevice->DrawIndexedPrimitiveVB(type, pVB, dwStartVertex, dwNumVertices, pwIndices, dwIndexCount, dwFlags);
}

// FUNCTION: D3DREN 0x1003a680
UnkType_VertexBufferPool::UnkType_VertexBufferPool() : m_Unk20(0x300), m_Unk24(1)
{
	m_Unk18 = 0;
	m_Unk1c = 0;
	m_Unk28 = 0;
	m_Unk2c = 0;
	m_Unk30 = 0;
	m_Unk34 = 0;
	m_Unk38 = 0;
}

// FUNCTION: D3DREN 0x1003a6ba ??_GUnkType_VertexBufferPool@@UAEPAXI@Z
// FUNCTION: D3DREN 0x1003a6d6
UnkType_VertexBufferPool::~UnkType_VertexBufferPool()
{
	FUN_1003a805();
}

// FUNCTION: D3DREN 0x1003a6fb
int UnkType_VertexBufferPool::FUN_1003a6fb(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices)
{
	int hr;

	if (m_Unk30 == 0)
		return 0;
	if (m_Unk34 == 0)
		return 0;
	m_Unk04[m_Unk18]->Unlock();
	m_Unk38 = 0;
	m_Unk34 = 0;
	if (nVertices == 0)
		return 0;
	hr = UnkInline_DrawPrimitiveVB(pDevice, type, m_Unk04[m_Unk18], m_Unk1c, nVertices, 0);
	m_Unk1c += nVertices;
	if (m_Unk1c >= m_Unk20)
	{
		FUN_1003a76c();
		m_Unk1c = 0;
	}
	return hr;
}

// The exe calls this out of line from its three callers (FUN_1003a6fb, FUN_1003a77c, FUN_1003a7f7).
// Matching compiler control: the verified callers in this object keep an out-of-line call to this function.
#pragma auto_inline(off)
// FUNCTION: D3DREN 0x1003a76c
void UnkType_VertexBufferPool::FUN_1003a76c()
{
	m_Unk18++;
	if (m_Unk18 >= m_Unk24)
		m_Unk18 = 0;
}
#pragma auto_inline(on)

// FUNCTION: D3DREN 0x1003a77c
int UnkType_VertexBufferPool::FUN_1003a77c(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices, uint16 *pIndices, uint32 nIndices)
{
	int hr;

	if (m_Unk30 == 0)
		return 0;
	if (m_Unk34 == 0)
		return 0;
	m_Unk04[m_Unk18]->Unlock();
	m_Unk38 = 0;
	m_Unk34 = 0;
	if (nVertices == 0)
		return 0;
	hr = UnkInline_DrawIndexedPrimitiveVB(pDevice, type, m_Unk04[m_Unk18], m_Unk1c, nVertices, pIndices, nIndices, 0);
	m_Unk1c += nVertices;
	if (m_Unk1c >= m_Unk20)
	{
		FUN_1003a76c();
		m_Unk1c = 0;
	}
	return hr;
}

// FUNCTION: D3DREN 0x1003a7f7
void UnkType_VertexBufferPool::FUN_1003a7f7()
{
	FUN_1003a76c();
	m_Unk1c = 0;
}

// FUNCTION: D3DREN 0x1003a805
void UnkType_VertexBufferPool::FUN_1003a805()
{
	if (m_Unk30)
	{
		if (m_Unk34)
		{
			m_Unk04[m_Unk18]->Unlock();
			m_Unk34 = 0;
			m_Unk38 = 0;
		}
		FUN_1003a90f();
		m_Unk30 = 0;
		m_Unk18 = 0;
		m_Unk1c = 0;
	}
}

// FUNCTION: D3DREN 0x1003a83d
int UnkType_VertexBufferPool::FUN_1003a83d(IDirect3D7 *pD3D, uint32 nVertices, uint32 nBuffers, int bUntransformed, int bHardware)
{
	int bOK;

	if (m_Unk30 == 0 || nVertices != m_Unk20 || nBuffers != m_Unk24)
	{
		FUN_1003a805();
		m_Unk20 = nVertices > 0 ? nVertices : m_Unk20;
		m_Unk24 = nBuffers > 0 ? nBuffers : m_Unk24;
		m_Unk28 = bUntransformed;
		m_Unk2c = bHardware;
		m_Unk04.Init(nBuffers, 0);
		bOK = vfn_Unk1c(pD3D);
		if (bOK)
		{
			bOK = 1;
			m_Unk30 = 1;
		}
		return bOK;
	}
	return 1;
}

// FUNCTION: D3DREN 0x1003a8b1
// guess: locks the current buffer and returns the address of the vertex at the current offset (NOOVERWRITE when
// appending, DISCARDCONTENTS at the start of the buffer).  vfn_Unk18() is the vertex size.
int UnkType_VertexBufferPool::Lock()
{
	DWORD flags;

	if (m_Unk34 != 0)
		return m_Unk38;
	flags = m_Unk1c != 0 ? DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOOVERWRITE : DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_DISCARDCONTENTS;
	int *pData = &m_Unk38;
	HRESULT hr = m_Unk04[m_Unk18]->Lock(flags, (void **)pData, 0);
	if (FAILED(hr))
		return 0;
	int base = *pData;
	int off = m_Unk1c;
	int p = vfn_Unk18() * off + base;
	*pData = p;
	m_Unk34 = 1;
	return p;
}

// FUNCTION: D3DREN 0x1003a90f
void UnkType_VertexBufferPool::FUN_1003a90f()
{
	uint32 i;

	for (i = 0; i < m_Unk04.GetSize(); i++)
	{
		if (m_Unk04[i])
			m_Unk04[i]->Release();
	}
	m_Unk04.Term();
}

// The model vertex buffer cache object (the static member of UnkType_ModelVBCacheHolder, vbcache.h).  Its destructor runs from an
// atexit function with an exit-once guard (flag byte 0x10093b7c, bit 0): the pattern VC6 produces for a static data member.
// FUNCTION: D3DREN 0x1003a942 _$E6
// FUNCTION: D3DREN 0x1003a958 _$E3
// FUNCTION: D3DREN 0x1003a973 ??1UnkType_VertexBufferCache@@UAE@XZ
UnkType_VertexBufferCache UnkType_ModelVBCacheHolder::DAT_10093b10;

// FUNCTION: D3DREN 0x1003a994
// FUNCTION: D3DREN 0x1003a9d0 ?vfn_Unk18@UnkType_VertexBufferCache@@UAEHXZ
// FUNCTION: D3DREN 0x1003a9d4 ??_GUnkType_VertexBufferCache@@UAEPAXI@Z
UnkType_VertexBufferCache::UnkType_VertexBufferCache() : m_Unk60(4)
{
	m_Unk50 = 0;
	m_Unk54 = 0;
	m_Unk58 = 0;
	m_Unk5c = 0;
	m_Unk64 = 0;
	m_Unk68 = 0;
	m_Unk28 = 1;
}

// FUNCTION: D3DREN 0x1003a9f0
int UnkType_VertexBufferCache::Lock()
{
	D3DVERTEXBUFFERDESC desc;
	LPDIRECT3DVERTEXBUFFER7 pVB;
	int bOK = 1;

	if (m_Unk04[m_Unk18] == 0)
	{
		memset(&desc, 0, sizeof(desc));
		desc.dwSize = sizeof(desc);
		desc.dwCaps = D3DVBCAPS_WRITEONLY;
		if (m_Unk2c == 0)
			desc.dwCaps = D3DVBCAPS_WRITEONLY | D3DVBCAPS_SYSTEMMEMORY;
		desc.dwFVF = m_Unk58;
		desc.dwNumVertices = m_Unk20;
		if (SUCCEEDED(m_Unk64->CreateVertexBuffer(&desc, &pVB, 0)))
			m_Unk04[m_Unk18] = pVB;
		else
			bOK = 0;
	}
	if (bOK)
		return UnkType_VertexBufferPool::Lock();
	return 0;
}

// FUNCTION: D3DREN 0x1003aa6d
int UnkType_VertexBufferCache::vfn_Unk1c(IDirect3D7 *pD3D)
{
	uint32 i;

	m_Unk64 = pD3D;
	m_Unk3c.SetSize(m_Unk24);
	for (i = 0; i < m_Unk24; i++)
	{
		UnkType_VBCacheEntry &e = m_Unk3c[i];
		e.m_Unk00 = 0;
		e.m_Unk0c = 0;
		e.m_Unk08 = 0;
		e.m_Unk04 = 0;
		m_Unk04[i] = 0;
	}
	return 1;
}

// FUNCTION: D3DREN 0x1003aabb
int UnkType_VertexBufferCache::FUN_1003aabb(uint32 nKey1, uint32 nKey2)
{
	uint32 i;

	if (m_Unk5c != 0)
		FUN_1003ac15();
	for (i = 0; i < m_Unk24; i++)
	{
		if (m_Unk3c[i].m_Unk00 == nKey1 && m_Unk3c[i].m_Unk04 == nKey2)
		{
			m_Unk1c = 0;
			m_Unk18 = i;
			return 1;
		}
	}
	return 0;
}

// guess: claim the next free entry (age 0) round-robin from m_Unk50 for the two keys; when every entry is busy and bGrow is set,
// append a new vertex buffer slot and entry instead.  Returns 0 when nothing could be claimed.
// The initial free slot must bypass growth (target 0x1003ab28 -> 0x1003ab44); only a full occupied ring grows
// (0x1003ab37 -> 0x1003abb0). Keep the full-ring check inside the occupied-slot guard.
// It remains STUB: the candidate is 275 bytes but 221 bytes differ (84 aligned instruction mismatches). VC6 emits grow/fail
// before found/tail, while the target branches to grow from the scan and lets a found slot fall through to the shared tail.
// STUB: D3DREN 0x1003ab02
int UnkType_VertexBufferCache::FUN_1003ab02(uint32 nKey1, uint32 nKey2, uint32 nVertices, int bGrow)
{
	if (m_Unk5c != 0)
		FUN_1003ac15();
	uint32 i = m_Unk50;
	if (m_Unk3c[i].m_Unk0c != 0)
	{
		do
		{
			i++;
			if (i >= m_Unk24)
				i = 0;
		}
		while (i != m_Unk50 && m_Unk3c[i].m_Unk0c != 0);
		if (i == m_Unk50)
			goto Grow;
	}
	m_Unk18 = i;
	if (m_Unk04[i])
	{
		m_Unk04[i]->Release();
		m_Unk04[m_Unk18] = 0;
	}
	m_Unk3c[i].m_Unk00 = nKey1;
	m_Unk3c[i].m_Unk04 = nKey2;
Tail:
	i = m_Unk18 + 1;
	m_Unk50 = i;
	if (i >= m_Unk24)
		m_Unk50 = 0;
	m_Unk5c = 1;
	m_Unk1c = 0;
	m_Unk20 = nVertices > 0 ? nVertices : m_Unk20;
	m_Unk3c[m_Unk18].m_Unk08 = nVertices;
	return 1;
Grow:
	if (bGrow != 0)
	{
		LPDIRECT3DVERTEXBUFFER7 pNull = 0;
		if (m_Unk04.Append(pNull))
		{
			m_Unk18 = m_Unk24;
			m_Unk24 = m_Unk24 + 1 > 0 ? m_Unk24 + 1 : m_Unk24;
			UnkType_VBCacheEntry entry(nKey1, nKey2, nVertices, m_Unk60);
			m_Unk3c.Append(entry);
			goto Tail;
		}
	}
	return 0;
}

// FUNCTION: D3DREN 0x1003ac15
void UnkType_VertexBufferCache::FUN_1003ac15()
{
	m_Unk5c = 0;
	m_Unk04[m_Unk18]->Optimize(m_Unk68, 0);
}

// FUNCTION: D3DREN 0x1003ac2e
int UnkType_VertexBufferCache::FUN_1003a6fb(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices)
{
	int hr;
	uint32 iSaved;

	m_Unk68 = pDevice;
	m_Unk3c[m_Unk18].m_Unk0c = m_Unk60;
	if (m_Unk5c != 0)
	{
		iSaved = m_Unk18;
		hr = UnkType_VertexBufferPool::FUN_1003a6fb(pDevice, type, nVertices);
		m_Unk18 = iSaved;
	}
	else
	{
		hr = UnkInline_DrawPrimitiveVB(pDevice, type, m_Unk04[m_Unk18], m_Unk1c, nVertices, 0);
		m_Unk1c += nVertices;
	}
	return hr;
}

// FUNCTION: D3DREN 0x1003ac8f
void UnkType_VertexBufferCache::FUN_1003a805()
{
	UnkType_VertexBufferPool::FUN_1003a805();
	m_Unk3c.Term();
	m_Unk50 = 0;
	m_Unk5c = 0;
}

// FUNCTION: D3DREN 0x1003acb0
void UnkType_VertexBufferCache::FUN_1003acb0()
{
	uint32 i;

	for (i = 0; i < m_Unk3c.GetSize(); i++)
	{
		if (m_Unk3c[i].m_Unk0c > 0)
			m_Unk3c[i].m_Unk0c--;
	}
}


// ------------------------------------------------------------------ //
// Template code emitted with this file: the CMoArray instances of the two pools (vtables 0x1004653c and 0x1004656c).
// The copies of GenBegin/GenIsValid/GenGetNext/GenGetSize/GenFindElement/GenSetCacheSize, Insert2/Remove2/_DeleteAndDestroyArray
// of the first array and _DeleteAndDestroyArray of the second one are identical to those of other CMoArray instances and were
// folded by the linker into copies elsewhere (0x1000e0c1.., 0x1001e3be.., 0x1001e407, 0x1001e4c6, 0x1001e57c).
// ------------------------------------------------------------------ //

// FUNCTION: D3DREN 0x1003acd9 ??0?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1003acfb ?GenGetAt@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UBEPAUIDirect3DVertexBuffer7@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003ad0a ?GenAppend@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UAEHAAPAUIDirect3DVertexBuffer7@@@Z
// FUNCTION: D3DREN 0x1003ad1f ?GenRemoveAt@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003ad30 ?GenRemoveAll@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1003ad3d ?GenCopyList@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UAEHABV?$GenList@PAUIDirect3DVertexBuffer7@@@@@Z
// FUNCTION: D3DREN 0x1003adc6 ?GenAppendList@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@UAEHABV?$GenList@PAUIDirect3DVertexBuffer7@@@@@Z
// FUNCTION: D3DREN 0x1003ae54 ??0?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@QAE@XZ
// FUNCTION: D3DREN 0x1003ae76 ?GenGetAt@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UBE?AUUnkType_VBCacheEntry@@AAVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003ae93 ?GenAppend@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UAEHAAUUnkType_VBCacheEntry@@@Z
// FUNCTION: D3DREN 0x1003aea8 ?GenRemoveAt@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UAEXVGenListPos@@@Z
// FUNCTION: D3DREN 0x1003aeb9 ?GenRemoveAll@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UAEXXZ
// FUNCTION: D3DREN 0x1003aec6 ?GenCopyList@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UAEHABV?$GenList@UUnkType_VBCacheEntry@@@@@Z
// FUNCTION: D3DREN 0x1003af79 ?GenAppendList@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@UAEHABV?$GenList@UUnkType_VBCacheEntry@@@@@Z
// FUNCTION: D3DREN 0x1003b034 ?Init@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1003b069 ?SetSize2@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b0c8 ?InternalNiceSetSize@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b177 ?Init@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@QAEHKK@Z
// FUNCTION: D3DREN 0x1003b1ac ?SetSize2@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@QAEHKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b20e ?InternalNiceSetSize@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@AAEHKHPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b2c9 ?_AllocateTArray@?$CMoArray@PAUIDirect3DVertexBuffer7@@VDefaultCache@@@@AAEPAPAUIDirect3DVertexBuffer7@@KPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b2e1 ?Insert2@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@QAEHKABUUnkType_VBCacheEntry@@PAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b3e2 ?Remove2@?$CMoArray@UUnkType_VBCacheEntry@@VDefaultCache@@@@QAEXKPAVLAlloc@@@Z
// FUNCTION: D3DREN 0x1003b4bd ?BaseNew@@YAPAUUnkType_VBCacheEntry@@PAVLAlloc@@PAU1@K@Z
