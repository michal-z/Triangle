#include "Pch.h"
#include "Renderer.h"


Renderer *s_Renderer;

Renderer::Renderer(HWND hwnd)
{
	IDXGIFactory4 *factory;
#ifdef _DEBUG
	VHR(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory)));
#else
	VHR(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
#endif

#ifdef _DEBUG
	{
		ID3D12Debug *dbg;
		D3D12GetDebugInterface(IID_PPV_ARGS(&dbg));
		if (dbg)
		{
			dbg->EnableDebugLayer();
			ID3D12Debug1 *dbg1;
			dbg->QueryInterface(IID_PPV_ARGS(&dbg1));
			if (dbg1)
				dbg1->SetEnableGPUBasedValidation(TRUE);
			SAFE_RELEASE(dbg);
			SAFE_RELEASE(dbg1);
		}
	}
#endif
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&Gpu))))
	{
		// #TODO: Add MessageBox
		return;
	}

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VHR(Gpu->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&CmdQueue)));

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = 4;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = hwnd;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Windowed = TRUE;

	IDXGISwapChain *tempSwapChain;
	VHR(factory->CreateSwapChain(CmdQueue, &swapChainDesc, &tempSwapChain));
	VHR(tempSwapChain->QueryInterface(IID_PPV_ARGS(&m_SwapChain)));
	SAFE_RELEASE(tempSwapChain);
	SAFE_RELEASE(factory);

	for (uint32_t i = 0; i < 2; ++i)
		VHR(Gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_CmdAlloc[i])));

	DescriptorSize = Gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	DescriptorSizeRtv = Gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	/* swap buffers */ {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 4;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(Gpu->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_SwapBufferHeap)));
		m_SwapBufferHeapStart = m_SwapBufferHeap->GetCPUDescriptorHandleForHeapStart();

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(m_SwapBufferHeapStart);

		for (uint32_t i = 0; i < 4; ++i)
		{
			VHR(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapBuffers[i])));

			Gpu->CreateRenderTargetView(m_SwapBuffers[i], nullptr, handle);
			handle.Offset(DescriptorSizeRtv);
		}
	}
	/* depth buffer */ {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(Gpu->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_DepthBufferHeap)));
		m_DepthBufferHeapStart = m_DepthBufferHeap->GetCPUDescriptorHandleForHeapStart();

		RECT r;
		GetClientRect(hwnd, &r);
		CD3DX12_RESOURCE_DESC imageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, r.right - r.left, r.bottom - r.top);
		imageDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		VHR(Gpu->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
			&imageDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&DepthBuffer)));

		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
		viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		viewDesc.Flags = D3D12_DSV_FLAG_NONE;
		Gpu->CreateDepthStencilView(DepthBuffer, &viewDesc, m_DepthBufferHeapStart);
	}

	VHR(Gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CmdAlloc[0], nullptr, IID_PPV_ARGS(&CmdList)));
	VHR(CmdList->Close());

	VHR(Gpu->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_FrameFence)));
	m_FrameFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
}

Renderer::~Renderer()
{
	SAFE_RELEASE(CmdList);
	SAFE_RELEASE(m_CmdAlloc[0]);
	SAFE_RELEASE(m_CmdAlloc[1]);
	SAFE_RELEASE(m_SwapBufferHeap);
	SAFE_RELEASE(m_DepthBufferHeap);
	SAFE_RELEASE(DepthBuffer);
	for (int i = 0; i < 4; ++i)
		SAFE_RELEASE(m_SwapBuffers[i]);
	CloseHandle(m_FrameFenceEvent);
	SAFE_RELEASE(m_FrameFence);
	SAFE_RELEASE(m_SwapChain);
	SAFE_RELEASE(CmdQueue);
	SAFE_RELEASE(Gpu);
}

void Renderer::Present()
{
	assert(CmdQueue);

	m_SwapChain->Present(0, 0);
	CmdQueue->Signal(m_FrameFence, ++m_FrameCount);

	const uint64_t gpuFrameCount = m_FrameFence->GetCompletedValue();

	if ((m_FrameCount - gpuFrameCount) >= 2)
	{
		m_FrameFence->SetEventOnCompletion(gpuFrameCount + 1, m_FrameFenceEvent);
		WaitForSingleObject(m_FrameFenceEvent, INFINITE);
	}

	m_FrameIndex = !m_FrameIndex;
	m_BackBufferIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void Renderer::Flush()
{
	assert(CmdQueue);

	CmdQueue->Signal(m_FrameFence, ++m_FrameCount);
	m_FrameFence->SetEventOnCompletion(m_FrameCount, m_FrameFenceEvent);
	WaitForSingleObject(m_FrameFenceEvent, INFINITE);
}
