#pragma once

#define VHR(hr) if (FAILED(hr)) { assert(0); }
#define SAFE_RELEASE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

class Renderer
{
public:
	Renderer(HWND hwnd);
	~Renderer();

	ID3D12Device *Gpu;
	ID3D12CommandQueue *CmdQueue;
	ID3D12CommandAllocator *CmdAlloc() const;
	ID3D12GraphicsCommandList *CmdList;
	uint32_t DescriptorSize;
	uint32_t DescriptorSizeRtv;
	ID3D12Resource *DepthBuffer;
	ID3D12Resource *BackBuffer() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE DepthBufferDescriptor() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE BackBufferDescriptor() const;

	void Present();
	void Flush();

private:
	ID3D12CommandAllocator *m_CmdAlloc[2];
	IDXGISwapChain3 *m_SwapChain;
	ID3D12DescriptorHeap *m_SwapBufferHeap;
	ID3D12DescriptorHeap *m_DepthBufferHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SwapBufferHeapStart;
	D3D12_CPU_DESCRIPTOR_HANDLE m_DepthBufferHeapStart;
	ID3D12Resource *m_SwapBuffers[4];
	ID3D12Fence *m_FrameFence;
	HANDLE m_FrameFenceEvent;
	uint32_t m_FrameIndex;
	uint32_t m_BackBufferIndex;
	uint64_t m_FrameCount;
};

// Renderer singleton
extern Renderer *s_Renderer;

inline ID3D12CommandAllocator *Renderer::CmdAlloc() const
{
	return m_CmdAlloc[m_FrameIndex];
}

inline ID3D12Resource *Renderer::BackBuffer() const
{
	return m_SwapBuffers[m_BackBufferIndex];
}

inline CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::DepthBufferDescriptor() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_DepthBufferHeapStart);
}

inline CD3DX12_CPU_DESCRIPTOR_HANDLE Renderer::BackBufferDescriptor() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_SwapBufferHeapStart, m_BackBufferIndex, DescriptorSizeRtv);
}