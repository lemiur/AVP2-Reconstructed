// d3d.ren model vertex buffer cache: the pool class that keeps one vertex buffer per cached model piece (global 0x10093b10).
// Split from vbpool.h because the object is a STATIC DATA MEMBER (its destructor has an exit-once guard, flag byte 0x10093b7c) and
// a static class-type member declared in a header uses up a `_$E` number in every unit that includes it: include this header only
// after the unit's own static initialisers (units unk/1003a680 and unk/10001000).
//
// All names are invented (UnkType_*, m_Unk<offset>); the roles are in the guess comments.  The holder class is invented too: the
// exe only shows the guarded destructor (the pattern VC6 produces for a static data member), not the owning class.
#ifndef __D3DREN_VBCACHE_H__
#define __D3DREN_VBCACHE_H__

#include "d3dren/vbpool.h"

// One entry of the cache's array (CMoArray<UnkType_VBCacheEntry>, vtable 0x1004656c): the vertex buffer of the same index
// in the base class array belongs to the model piece named by the two keys.  The constructor zeroes the four members in
// this order (BaseNew's constructing loop, 0x1003b4bd).
struct UnkType_VBCacheEntry
{
	uint32	m_Unk00;	// 0x00 guess: key 1
	uint32	m_Unk04;	// 0x04 guess: key 2
	uint32	m_Unk08;	// 0x08 guess: vertex count of the cached data
	uint32	m_Unk0c;	// 0x0c guess: age: frames the entry is still protected from reuse (AgeEntries counts it down)

	UnkType_VBCacheEntry()
	{
		m_Unk00 = 0;
		m_Unk04 = 0;
		m_Unk08 = 0;
		m_Unk0c = 0;
	}

	UnkType_VBCacheEntry(uint32 nKey1, uint32 nKey2, uint32 nVertices, uint32 nAge)		// guess: AllocEntry builds the entry of a newly added buffer with this
	{
		m_Unk00 = nKey1;
		m_Unk04 = nKey2;
		m_Unk08 = nVertices;
		m_Unk0c = nAge;
	}
};

// The model vertex buffer cache (vtable 0x1004659c, object 0x10093b10, 0x6c bytes): one vertex buffer per cached model piece,
// looked up by two keys (FindEntry) or recycled round-robin (AllocEntry).  The virtual slots keep the names of the base class
// slots they override; GetVertexSize returns the vertex size that the model drawer stores at +0x54, +0x58 holds the FVF.
// The destructor is compiler generated (FUN_1003a973, scalar deleting destructor FUN_1003a9d4): it does not reset the vptr.
class UnkType_VertexBufferCache : public UnkType_VertexBufferPool
{
public:
	UnkType_VertexBufferCache();				// FUN_1003a994 (out of line)

	virtual int		DrawPrimitive(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices);	// slot 1 override FUN_1003ac2e
	virtual void	Term();												// slot 3 override FUN_1003ac8f
	virtual int		Lock();												// slot 5 override Lock
	virtual int		GetVertexSize() { return m_Unk54; }								// slot 6 FUN_1003a9d0
	virtual int		CreateVertexBuffers(IDirect3D7 *pD3D);								// slot 7 FUN_1003aa6d

	int				SelectEntry(uint32 nKey1, uint32 nKey2);							// guess: find the entry with these keys and make it the current buffer
	int				AllocateEntry(uint32 nKey1, uint32 nKey2, uint32 nVertices, int bGrow);	// guess: claim an entry for these keys (grow the pool when bGrow)
	void			OptimizeCurrentBuffer();												// guess: optimize the filled buffer (Optimize(m_Unk68, 0))
	void			AgeEntries();												// guess: age every entry by one frame

	CMoArray<UnkType_VBCacheEntry>	m_Unk3c;	// 0x3c  guess: one entry per buffer (0x3c-0x4f)
	uint32	m_Unk50;	// 0x50  guess: round-robin position of the next entry to try
	int		m_Unk54;	// 0x54  guess: vertex size in bytes (set by the model drawer)
	uint32	m_Unk58;	// 0x58  guess: FVF of the vertices (set by the model drawer)
	int		m_Unk5c;	// 0x5c  guess: the current buffer was filled and still has to be optimized
	uint32	m_Unk60;	// 0x60  guess: initial age of an entry (4 after the constructor; the ModelVBCacheDelay console variable)
	IDirect3D7			*m_Unk64;	// 0x64 the Direct3D object that creates the buffers (stored by CreateVertexBuffers)
	IDirect3DDevice7	*m_Unk68;	// 0x68 the device of the last draw call
};

// The owner of the one instance of the cache (defined in unk/1003a680).  Ghidra name of the object: s_ModelVertexBufferCache.
class UnkType_ModelVBCacheHolder
{
public:
	// GLOBAL: D3DREN 0x10093b10 ?s_ModelVertexBufferCache@UnkType_ModelVBCacheHolder@@2VUnkType_VertexBufferCache@@A
	static UnkType_VertexBufferCache s_ModelVertexBufferCache;
};

// GLOBAL: D3DREN 0x10054888
extern uint16 g_ModelVBCacheLastAgeFrameCode;		// guess: the frame code (g_CurFrameCode) at which the vertex buffer cache was last aged

#endif
