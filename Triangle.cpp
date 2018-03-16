#define _CRT_SECURE_NO_WARNINGS
#include <assert.h>
#include <stdint.h>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <d3d12.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include "d3dx12.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
using namespace DirectX;


#define VHR(hr) if (FAILED(hr)) { assert(0); }
#define SAFE_RELEASE(obj) if ((obj)) { (obj)->Release(); (obj) = nullptr; }

#define k_Name "Triangle"
#define k_ResolutionX 1920
#define k_ResolutionY 1080
#define k_SwapBufferCount 4
#define k_TriangleCount 8

static ID3D12Device *s_Gpu;
static ID3D12CommandQueue *s_CmdQueue;
static ID3D12CommandAllocator *s_CmdAlloc[2];
static ID3D12GraphicsCommandList *s_CmdList;
static IDXGISwapChain3 *s_SwapChain;
static uint32_t s_DescriptorSize;
static uint32_t s_DescriptorSizeRtv;
static ID3D12DescriptorHeap *s_SwapBufferHeap;
static ID3D12DescriptorHeap *s_DepthBufferHeap;
static D3D12_CPU_DESCRIPTOR_HANDLE s_SwapBufferHeapStart;
static D3D12_CPU_DESCRIPTOR_HANDLE s_DepthBufferHeapStart;
static ID3D12Resource *s_SwapBuffers[k_SwapBufferCount];
static ID3D12Resource *s_DepthBuffer;
static ID3D12PipelineState *s_Pso;
static ID3D12RootSignature *s_Rs;
static ID3D12Resource *s_TriangleVb;
static uint32_t s_FrameIndex;
static uint32_t s_BackBufferIndex;
static ID3D12Fence *s_FrameFence;
static HANDLE s_FrameFenceEvent;

static double GetTime()
{
	static LARGE_INTEGER frequency;
	static LARGE_INTEGER startCounter;
	if (frequency.QuadPart == 0)
	{
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&startCounter);
	}
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return (counter.QuadPart - startCounter.QuadPart) / (double)frequency.QuadPart;
}

static std::vector<uint8_t> LoadFile(const char *fileName)
{
	FILE *file = fopen(fileName, "rb");
	assert(file);
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	assert(size != -1);
	std::vector<uint8_t> content(size);
	fseek(file, 0, SEEK_SET);
	fread(&content[0], 1, content.size(), file);
	fclose(file);
	return content;
}

static void UpdateFrameTime(HWND window, const char *windowText, double *time, float *deltaTime)
{
	static double lastTime = -1.0;
	static double lastFpsTime = 0.0;
	static uint32_t frameCount = 0;

	if (lastTime < 0.0)
	{
		lastTime = GetTime();
		lastFpsTime = lastTime;
	}

	*time = GetTime();
	*deltaTime = (float)(*time - lastTime);
	lastTime = *time;

	if ((*time - lastFpsTime) >= 1.0)
	{
		double fps = frameCount / (*time - lastFpsTime);
		double ms = (1.0 / fps) * 1000.0;
		char text[256];
		snprintf(text, sizeof(text), "[%.1f fps  %.3f ms] %s", fps, ms, windowText);
		SetWindowText(window, text);
		lastFpsTime = *time;
		frameCount = 0;
	}
	frameCount++;
}

static LRESULT CALLBACK ProcessWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	case WM_KEYDOWN:
		if (wparam == VK_ESCAPE)
		{
			PostQuitMessage(0);
			return 0;
		}
		break;
	}
	return DefWindowProc(window, message, wparam, lparam);
}

static HWND MakeWindow(const char *name, uint32_t resolutionX, uint32_t resolutionY)
{
	WNDCLASS winclass = {};
	winclass.lpfnWndProc = ProcessWindowMessage;
	winclass.hInstance = GetModuleHandle(nullptr);
	winclass.hCursor = LoadCursor(nullptr, IDC_ARROW);
	winclass.lpszClassName = name;
	if (!RegisterClass(&winclass))
		assert(0);

	RECT rect = { 0, 0, (LONG)resolutionX, (LONG)resolutionY };
	if (!AdjustWindowRect(&rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
		assert(0);

	HWND window = CreateWindowEx(
		0, name, name, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right - rect.left, rect.bottom - rect.top,
		nullptr, nullptr, nullptr, 0);
	assert(window);

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
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_1, IID_PPV_ARGS(&s_Gpu))))
	{
		return false;
	}

	D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {};
	cmdQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cmdQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	cmdQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	VHR(s_Gpu->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&s_CmdQueue)));

	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = k_SwapBufferCount;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = window;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	swapChainDesc.Windowed = TRUE;

	IDXGISwapChain *tempSwapChain;
	VHR(factory->CreateSwapChain(s_CmdQueue, &swapChainDesc, &tempSwapChain));
	VHR(tempSwapChain->QueryInterface(IID_PPV_ARGS(&s_SwapChain)));
	SAFE_RELEASE(tempSwapChain);
	SAFE_RELEASE(factory);

	for (uint32_t i = 0; i < 2; ++i)
		VHR(s_Gpu->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_CmdAlloc[i])));

	s_DescriptorSize = s_Gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	s_DescriptorSizeRtv = s_Gpu->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	/* swap buffers */ {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = k_SwapBufferCount;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(s_Gpu->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_SwapBufferHeap)));
		s_SwapBufferHeapStart = s_SwapBufferHeap->GetCPUDescriptorHandleForHeapStart();

		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(s_SwapBufferHeapStart);

		for (uint32_t i = 0; i < k_SwapBufferCount; ++i)
		{
			VHR(s_SwapChain->GetBuffer(i, IID_PPV_ARGS(&s_SwapBuffers[i])));

			s_Gpu->CreateRenderTargetView(s_SwapBuffers[i], nullptr, handle);
			handle.Offset(s_DescriptorSizeRtv);
		}
	}
	/* depth buffer */ {
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		VHR(s_Gpu->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_DepthBufferHeap)));
		s_DepthBufferHeapStart = s_DepthBufferHeap->GetCPUDescriptorHandleForHeapStart();

		CD3DX12_RESOURCE_DESC imageDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, k_ResolutionX, k_ResolutionY);
		imageDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		VHR(s_Gpu->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
			&imageDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&CD3DX12_CLEAR_VALUE(DXGI_FORMAT_D32_FLOAT, 1.0f, 0), IID_PPV_ARGS(&s_DepthBuffer)));

		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc = {};
		viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
		viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		viewDesc.Flags = D3D12_DSV_FLAG_NONE;
		s_Gpu->CreateDepthStencilView(s_DepthBuffer, &viewDesc, s_DepthBufferHeapStart);
	}

	return window;
}

static void PresentFrame()
{
	assert(s_CmdQueue);

	static uint64_t cpuFrameCount = 0;

	s_SwapChain->Present(0, 0);
	s_CmdQueue->Signal(s_FrameFence, ++cpuFrameCount);

	const uint64_t gpuFrameCount = s_FrameFence->GetCompletedValue();

	if ((cpuFrameCount - gpuFrameCount) >= 2)
	{
		s_FrameFence->SetEventOnCompletion(gpuFrameCount + 1, s_FrameFenceEvent);
		WaitForSingleObject(s_FrameFenceEvent, INFINITE);
	}

	s_FrameIndex = !s_FrameIndex;
	s_BackBufferIndex = s_SwapChain->GetCurrentBackBufferIndex();
}

static void Setup()
{
	/* pso */ {
		std::vector<uint8_t> vsCode = LoadFile("TriangleVs.cso");
		std::vector<uint8_t> psCode = LoadFile("TrianglePs.cso");

		D3D12_INPUT_ELEMENT_DESC inputLayoutDesc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayoutDesc, 1 };
		psoDesc.VS = { vsCode.data(), vsCode.size() };
		psoDesc.PS = { psCode.data(), psCode.size() };
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.AntialiasedLineEnable = 1;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.SampleMask = 0xffffffff;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;

		VHR(s_Gpu->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&s_Pso)));
		VHR(s_Gpu->CreateRootSignature(0, vsCode.data(), vsCode.size(), IID_PPV_ARGS(&s_Rs)));
	}
	/* vertex buffer */ {
		VHR(s_Gpu->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(k_TriangleCount * 4 * sizeof(XMFLOAT3)),
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_TriangleVb)));

		XMFLOAT3 *ptr;
		float size = 0.7f;
		VHR(s_TriangleVb->Map(0, &CD3DX12_RANGE(0, 0), (void **)&ptr));
		for (int32_t i = 0; i < k_TriangleCount; ++i)
		{
			*ptr++ = XMFLOAT3(-size, -size, 0.0f);
			*ptr++ = XMFLOAT3(size, -size, 0.0f);
			*ptr++ = XMFLOAT3(0.0f, size, 0.0f);
			*ptr++ = XMFLOAT3(-size, -size, 0.0f);
			size -= 0.1f;
		}
		s_TriangleVb->Unmap(0, nullptr);
	}

	VHR(s_Gpu->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_CmdAlloc[0], nullptr, IID_PPV_ARGS(&s_CmdList)));
	VHR(s_CmdList->Close());

	VHR(s_Gpu->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_FrameFence)));
	s_FrameFenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
}

static void DrawFrame()
{	
	s_CmdAlloc[s_FrameIndex]->Reset();

	s_CmdList->Reset(s_CmdAlloc[s_FrameIndex], nullptr);
	s_CmdList->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)k_ResolutionX, (float)k_ResolutionY));
	s_CmdList->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, k_ResolutionX, k_ResolutionY));

	s_CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(s_SwapBuffers[s_BackBufferIndex], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(s_SwapBufferHeapStart, s_BackBufferIndex, s_DescriptorSizeRtv);

	s_CmdList->OMSetRenderTargets(1, &rtvHandle, 0, &s_DepthBufferHeapStart);

	s_CmdList->ClearRenderTargetView(rtvHandle, XMVECTORF32{ 0.0f, 0.2f, 0.4f, 1.0f }, 0, nullptr);
	s_CmdList->ClearDepthStencilView(s_DepthBufferHeapStart, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	s_CmdList->SetPipelineState(s_Pso);
	s_CmdList->SetGraphicsRootSignature(s_Rs);

	//s_CmdList->SetGraphicsRootConstantBufferView(0, ID3D12Resource_GetGPUVirtualAddress(s_constant_buffer));
	s_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);

	s_CmdList->IASetVertexBuffers(0, 1, &D3D12_VERTEX_BUFFER_VIEW{ s_TriangleVb->GetGPUVirtualAddress(), k_TriangleCount * 4 * sizeof(XMFLOAT3), sizeof(XMFLOAT3) });
	for (int32_t i = 0; i < k_TriangleCount; ++i)
		s_CmdList->DrawInstanced(4, 1, i * 4, 0);

	s_CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(s_SwapBuffers[s_BackBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	VHR(s_CmdList->Close());

	s_CmdQueue->ExecuteCommandLists(1, (ID3D12CommandList **)&s_CmdList);
}

int32_t CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int32_t)
{
	SetProcessDPIAware();
	HWND window = MakeWindow(k_Name, k_ResolutionX, k_ResolutionY);

	Setup();

	for (;;)
	{
		MSG message = {};
		if (PeekMessage(&message, 0, 0, 0, PM_REMOVE))
		{
			DispatchMessage(&message);
			if (message.message == WM_QUIT)
				break;
		}
		else
		{
			double frameTime;
			float frameDeltaTime;
			UpdateFrameTime(window, k_Name, &frameTime, &frameDeltaTime);
			DrawFrame();
			PresentFrame();
		}
	}

	return 0;
}
