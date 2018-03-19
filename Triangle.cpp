#include "Pch.h"
#include "Renderer.h"
using namespace DirectX;


#define k_Name "Triangle"
#define k_ResolutionX 1280
#define k_ResolutionY 720
#define k_SwapBufferCount 4
#define k_TriangleCount 8

static ID3D12PipelineState *s_Pso;
static ID3D12RootSignature *s_Rs;
static ID3D12Resource *s_TriangleVb;
static ID3D12Resource *s_Cb;
static void *s_CbCpuAddr;

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
	return window;
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

		VHR(s_Renderer->Gpu->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&s_Pso)));
		VHR(s_Renderer->Gpu->CreateRootSignature(0, vsCode.data(), vsCode.size(), IID_PPV_ARGS(&s_Rs)));
	}
	/* vertex buffer */ {
		VHR(s_Renderer->Gpu->CreateCommittedResource(
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
	/* constant buffer */ {
		VHR(s_Renderer->Gpu->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sizeof(XMFLOAT4X4)),
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_Cb)));

		VHR(s_Cb->Map(0, &CD3DX12_RANGE(0, 0), &s_CbCpuAddr));
	}
}

static void Update(double time, float deltaTime)
{
	XMMATRIX objectToProj =
		XMMatrixRotationY((float)time) *
		XMMatrixLookAtLH(XMVectorSet(2.0f, 2.0f, 2.0f, 1.0f), XMVectorReplicate(0.0f), XMVectorSet(0.0, 1.0f, 0.0f, 0.0f)) *
		XMMatrixPerspectiveFovLH(XM_PI / 3, (float)k_ResolutionX / k_ResolutionY, 0.1f, 10.0f);

	XMStoreFloat4x4A((XMFLOAT4X4A *)s_CbCpuAddr, XMMatrixTranspose(objectToProj));
}

static void DrawFrame()
{
	ID3D12CommandAllocator *cmdalloc = s_Renderer->CmdAlloc();
	ID3D12GraphicsCommandList *cmdlist = s_Renderer->CmdList;

	cmdalloc->Reset();

	cmdlist->Reset(cmdalloc, nullptr);
	cmdlist->RSSetViewports(1, &CD3DX12_VIEWPORT(0.0f, 0.0f, (float)k_ResolutionX, (float)k_ResolutionY));
	cmdlist->RSSetScissorRects(1, &CD3DX12_RECT(0, 0, k_ResolutionX, k_ResolutionY));

	cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(s_Renderer->BackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	D3D12_CPU_DESCRIPTOR_HANDLE backBufferDescriptor = s_Renderer->BackBufferDescriptor();
	D3D12_CPU_DESCRIPTOR_HANDLE depthBufferDescriptor = s_Renderer->DepthBufferDescriptor();

	cmdlist->OMSetRenderTargets(1, &backBufferDescriptor, 0, &depthBufferDescriptor);

	cmdlist->ClearRenderTargetView(backBufferDescriptor, XMVECTORF32{ 0.0f, 0.2f, 0.4f, 1.0f }, 0, nullptr);
	cmdlist->ClearDepthStencilView(depthBufferDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	cmdlist->SetPipelineState(s_Pso);

	cmdlist->SetGraphicsRootSignature(s_Rs);
	cmdlist->SetGraphicsRootConstantBufferView(0, s_Cb->GetGPUVirtualAddress());
	
	cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
	cmdlist->IASetVertexBuffers(0, 1, &D3D12_VERTEX_BUFFER_VIEW{ s_TriangleVb->GetGPUVirtualAddress(), k_TriangleCount * 4 * sizeof(XMFLOAT3), sizeof(XMFLOAT3) });

	for (int32_t i = 0; i < k_TriangleCount; ++i)
		cmdlist->DrawInstanced(4, 1, i * 4, 0);

	cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(s_Renderer->BackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	VHR(cmdlist->Close());

	s_Renderer->CmdQueue->ExecuteCommandLists(1, (ID3D12CommandList **)&cmdlist);
}

int32_t CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int32_t)
{
	SetProcessDPIAware();
	HWND window = MakeWindow(k_Name, k_ResolutionX, k_ResolutionY);

	s_Renderer = new Renderer(window);

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
			Update(frameTime, frameDeltaTime);
			DrawFrame();
			s_Renderer->Present();
		}
	}

	s_Renderer->Flush();
	SAFE_RELEASE(s_Pso);
	SAFE_RELEASE(s_Rs);
	SAFE_RELEASE(s_TriangleVb);
	SAFE_RELEASE(s_Cb);
	delete s_Renderer;
	return 0;
}
