// d3d.ren model vertex buffer pools: a base class that owns an array of IDirect3DVertexBuffer7 and two derived
// classes that pick the vertex format (one texture coordinate set / two sets, transformed or untransformed).
//
// All type, member and slot names here are invented (no symbols survive in d3d.ren): UnkType_* classes,
// m_Unk<offset> members, vfn_Unk<offset> virtual slots.  The roles are in the guess comments.
//   vtable of the base class: 0x1004651c; derived vtables: 0x10046140 / 0x10046180 (A), 0x10046160 / 0x100461a0 (B).
#ifndef __D3DREN_VBPOOL_H__
#define __D3DREN_VBPOOL_H__

#include <windows.h>
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <ddraw.h>
#include <d3d.h>
#include "ltdynarray.h"

// The default allocator every CMoArray of this DLL uses (StdLith l_allocator.h `extern LAlloc g_DefAlloc;`).
// GLOBAL: D3DREN 0x10093b84
extern LAlloc g_DefAlloc;

class UnkType_VertexBufferPool
{
public:
	UnkType_VertexBufferPool();					// FUN_1003a680 (out of line)
	virtual ~UnkType_VertexBufferPool();		// FUN_1003a6d6 (out of line)

	virtual int		DrawPrimitive(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices);	// slot 1  guess: DrawPrimitiveVB of the current buffer (ret 0xc)
	virtual int		DrawIndexedPrimitive(IDirect3DDevice7 *pDevice, D3DPRIMITIVETYPE type, uint32 nVertices, uint16 *pIndices, uint32 nIndices);	// slot 2  guess: DrawIndexedPrimitiveVB (ret 0x14)
	virtual void	Term();												// slot 3  guess: release all buffers
	virtual int		Init(IDirect3D7 *pD3D, uint32 nVertices, uint32 nBuffers, int bUntransformed, int bHardware);	// slot 4  guess: (re)create the pool
	virtual int		Lock();												// slot 5
	virtual int		vfn_Unk18() = 0;											// slot 6  guess: vertex size in bytes
	virtual int		vfn_Unk1c(IDirect3D7 *pD3D) = 0;							// slot 7  guess: create the vertex buffers

	void			AdvanceBuffer();												// guess: advance to the next buffer (m_Unk18 wraps at m_Unk24)
	void			RestartInNextBuffer();												// guess: next buffer and restart at vertex 0 (m_Unk1c = 0)
	void			ReleaseVertexBuffers();												// guess: release the vertex buffers (non-virtual)

	CMoArray<LPDIRECT3DVERTEXBUFFER7>	m_Unk04;	// 0x04  guess: the vertex buffers
	uint32	m_Unk18;	// 0x18  guess: current buffer
	uint32	m_Unk1c;	// 0x1c  guess: current vertex offset
	uint32	m_Unk20;	// 0x20  guess: vertices per buffer (0x300 after the constructor)
	uint32	m_Unk24;	// 0x24  guess: buffer count (1 after the constructor)
	int		m_Unk28;	// 0x28  guess: vertices are untransformed (XYZ) instead of XYZRHW
	int		m_Unk2c;	// 0x2c  guess: hardware T&L (video memory buffers)
	int		m_Unk30;	// 0x30  guess: created
	int		m_Unk34;	// 0x34  guess: locked
	int		m_Unk38;	// 0x38  guess: address of the locked vertices of the current buffer (0 while unlocked)
};

// The exe has four vtables (0x10046140, 0x10046160, 0x10046180, 0x100461a0) with identical contents in pairs, so the original
// has four distinct classes whose identical members the linker folded.  Modelled as instances of a tag template: the
// parameter N is invented and only keeps the four vtables, constructors and inline virtuals distinct.
//   A<0>: 0x1004d620 (vtable 0x10046140)   A<1>: 0x1004eb48 (vtable 0x10046180)
//   B<0>: 0x1004da80 (vtable 0x10046160)   B<1>: 0x1004dae0 (vtable 0x100461a0)

// One texture coordinate set: 32 bytes (XYZRHW) or 28 bytes (XYZ) per vertex.
template <int N>
class UnkType_VertexBufferPoolA : public UnkType_VertexBufferPool
{
public:
	UnkType_VertexBufferPoolA(int bUntransformed) { m_Unk28 = bUntransformed; }

	virtual int vfn_Unk18() { return m_Unk28 ? 28 : 32; }
	virtual int vfn_Unk1c(IDirect3D7 *pD3D);
};

// Two texture coordinate sets: 40 bytes (XYZRHW) or 36 bytes (XYZ) per vertex.
template <int N>
class UnkType_VertexBufferPoolB : public UnkType_VertexBufferPool
{
public:
	UnkType_VertexBufferPoolB(int bUntransformed) { m_Unk28 = bUntransformed; }

	virtual int vfn_Unk18() { return m_Unk28 ? 36 : 40; }
	virtual int vfn_Unk1c(IDirect3D7 *pD3D);
};

#endif
