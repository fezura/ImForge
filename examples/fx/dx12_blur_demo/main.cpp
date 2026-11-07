// imb::fx blur on Win32 + DX12. Build from a vcvars64 prompt:
//   cl /std:c++17 /EHsc /W4 /I include /I <imgui> /I <imgui>/backends main.cpp <imgui>/imgui*.cpp
//      <imgui>/backends/imgui_impl_win32.cpp <imgui>/backends/imgui_impl_dx12.cpp
//      d3d12.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib
// --seconds N quits after N seconds, handy for automated runs
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imforge/fx/imb_fx_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

static const int kFramesInFlight = 3;
static const int kBackBuffers = 3;
static const int kSrvHeapSize = 64;
static const int kFxSrvCount = 16;
static const int kBlobCount = 7;

struct FrameContext {
    ID3D12CommandAllocator* allocator;
    UINT64 fence_value;
};

struct DescriptorAllocator {
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {};
    UINT increment = 0;
    ImVector<int> free_indices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* h, int count) {
        heap = h;
        cpu_start = heap->GetCPUDescriptorHandleForHeapStart();
        gpu_start = heap->GetGPUDescriptorHandleForHeapStart();
        increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (int n = count; n > 0; n--) free_indices.push_back(n - 1);
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
        IM_ASSERT(free_indices.Size > 0);
        int idx = free_indices.back();
        free_indices.pop_back();
        cpu->ptr = cpu_start.ptr + (SIZE_T)idx * increment;
        gpu->ptr = gpu_start.ptr + (UINT64)idx * increment;
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
        free_indices.push_back((int)((cpu.ptr - cpu_start.ptr) / increment));
    }
};

static FrameContext g_frame_context[kFramesInFlight] = {};
static UINT g_frame_index = 0;
static ID3D12Device* g_device = nullptr;
static ID3D12DescriptorHeap* g_rtv_heap = nullptr;
static ID3D12DescriptorHeap* g_srv_heap = nullptr;
static DescriptorAllocator g_srv_alloc;
static ID3D12CommandQueue* g_queue = nullptr;
static ID3D12GraphicsCommandList* g_cmd_list = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE g_fence_event = nullptr;
static UINT64 g_fence_last_value = 0;
static IDXGISwapChain3* g_swap_chain = nullptr;
static bool g_swap_chain_occluded = false;
static HANDLE g_swap_chain_waitable = nullptr;
static ID3D12Resource* g_back_buffer[kBackBuffers] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_back_buffer_rtv[kBackBuffers] = {};
static ID3D12RootSignature* g_scene_root_sig = nullptr;
static ID3D12PipelineState* g_scene_pso = nullptr;
#ifdef DX12_ENABLE_DEBUG_LAYER
static ID3D12InfoQueue* g_info_queue = nullptr;
#endif

static bool CreateDeviceD3D(HWND hwnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static void WaitForLastSubmittedFrame();
static FrameContext* WaitForNextFrameResources();
static bool CreateScenePipeline();
static void DrawScene(float time, float width, float height);
static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

int main(int argc, char** argv) {
    double exit_after = 0.0;
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--seconds") == 0) exit_after = atof(argv[i + 1]);

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr,
                       nullptr, nullptr, L"ImForgeFxDx12", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ImForge fx: DX12 blur demo", WS_OVERLAPPEDWINDOW, 100, 100,
                                1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(hwnd) || !CreateScenePipeline()) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 10.0f;
    ImGui_ImplWin32_Init(hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_device;
    init_info.CommandQueue = g_queue;
    init_info.NumFramesInFlight = kFramesInFlight;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = g_srv_heap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                        D3D12_GPU_DESCRIPTOR_HANDLE* gpu) { g_srv_alloc.Alloc(cpu, gpu); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpu) { g_srv_alloc.Free(cpu, gpu); };
    ImGui_ImplDX12_Init(&init_info);

    imb::fx::Dx12InitDesc fx = {};
    fx.device = g_device;
    fx.rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    fx.frames_in_flight = kFramesInFlight;
    fx.srv_heap = g_srv_heap;
    fx.srv_cpu_start = g_srv_alloc.cpu_start;
    fx.srv_gpu_start = g_srv_alloc.gpu_start;
    fx.srv_cpu_start.ptr += (SIZE_T)(kSrvHeapSize - kFxSrvCount) * g_srv_alloc.increment;
    fx.srv_gpu_start.ptr += (UINT64)(kSrvHeapSize - kFxSrvCount) * g_srv_alloc.increment;
    fx.srv_descriptor_count = kFxSrvCount;
    if (!imb::fx::Init(fx)) printf("imb::fx::Init failed: %s\n", imb::fx::Dx12LastError());

    imb::fx::BlurParams params;
    params.radius = 16.0f;
    params.rounding = 10.0f;
    params.tint = IM_COL32(200, 210, 255, 235);
    params.noise = 0.5f;
    float tint_rgba[4] = { 200 / 255.0f, 210 / 255.0f, 255 / 255.0f, 235 / 255.0f };
    ULONGLONG start_ticks = ::GetTickCount64();
    int frames_rendered = 0;

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (exit_after > 0.0 && (double)(::GetTickCount64() - start_ticks) >= exit_after * 1000.0) done = true;
        if (done) break;
        if (g_swap_chain_occluded && g_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_swap_chain_occluded = false;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("imb::fx blur (DX12)");
        {
            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 size = ImGui::GetWindowSize();
            imb::fx::Blur(ImGui::GetWindowDrawList(), pos, ImVec2(pos.x + size.x, pos.y + size.y), params);
        }
        ImGui::Text("available: %s   %.1f fps", imb::fx::IsAvailable() ? "yes" : "no", ImGui::GetIO().Framerate);
        ImGui::SliderFloat("radius", &params.radius, 0.0f, 64.0f);
        ImGui::SliderFloat("rounding", &params.rounding, 0.0f, 40.0f);
        if (ImGui::ColorEdit4("tint", tint_rgba))
            params.tint = ImGui::ColorConvertFloat4ToU32(ImVec4(tint_rgba[0], tint_rgba[1], tint_rgba[2], tint_rgba[3]));
        ImGui::SliderFloat("saturation", &params.saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("brightness", &params.brightness, 0.0f, 2.0f);
        ImGui::SliderFloat("noise", &params.noise, 0.0f, 1.0f);
        ImGui::TextWrapped("Window background alpha is 0; the blurred capture is drawn behind the content.");
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320, 140), ImGuiCond_FirstUseEver);
        ImGui::Begin("BlurCurrentWindow(12)");
        imb::fx::BlurCurrentWindow(12.0f);
        ImGui::TextWrapped("Default convenience call: radius 12, rounding from style.");
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::Render();

        FrameContext* frame_ctx = WaitForNextFrameResources();
        UINT bb_idx = g_swap_chain->GetCurrentBackBufferIndex();
        frame_ctx->allocator->Reset();

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = g_back_buffer[bb_idx];
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_cmd_list->Reset(frame_ctx->allocator, nullptr);
        g_cmd_list->ResourceBarrier(1, &barrier);

        D3D12_RESOURCE_DESC bb_desc = g_back_buffer[bb_idx]->GetDesc();
        float width = (float)bb_desc.Width;
        float height = (float)bb_desc.Height;
        const float clear_color[4] = { 0.07f, 0.08f, 0.12f, 1.0f };
        g_cmd_list->ClearRenderTargetView(g_back_buffer_rtv[bb_idx], clear_color, 0, nullptr);
        g_cmd_list->OMSetRenderTargets(1, &g_back_buffer_rtv[bb_idx], FALSE, nullptr);
        DrawScene((float)ImGui::GetTime(), width, height);

        imb::fx::FrameDesc fd;
        fd.cmd_list = g_cmd_list;
        fd.back_buffer = g_back_buffer[bb_idx];
        fd.width = (int)width;
        fd.height = (int)height;
        fd.frame_index = (int)(g_frame_index % kFramesInFlight);
        fd.back_buffer_rtv = g_back_buffer_rtv[bb_idx];
        imb::fx::PrepareFrame(fd);

        g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_cmd_list->ResourceBarrier(1, &barrier);
        g_cmd_list->Close();
        g_queue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_cmd_list);

        HRESULT hr = g_swap_chain->Present(1, 0);
        g_swap_chain_occluded = (hr == DXGI_STATUS_OCCLUDED);
        UINT64 fence_value = g_fence_last_value + 1;
        g_queue->Signal(g_fence, fence_value);
        g_fence_last_value = fence_value;
        frame_ctx->fence_value = fence_value;
        frames_rendered++;
    }

    WaitForLastSubmittedFrame();
    imb::fx::Shutdown();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    printf("frames rendered: %d\n", frames_rendered);
    return 0;
}

static const char* kSceneShader =
    "cbuffer C : register(b0) { float4 rect; float4 color; float4 display; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
    "VSOut VSMain(uint id : SV_VertexID) {\n"
    "  float2 p = float2((id & 1) ? rect.z : rect.x, (id & 2) ? rect.w : rect.y);\n"
    "  VSOut o;\n"
    "  o.pos = float4(p.x / display.x * 2.0 - 1.0, 1.0 - p.y / display.y * 2.0, 0.0, 1.0);\n"
    "  o.uv = float2((float)(id & 1), (float)((id >> 1) & 1));\n"
    "  return o;\n"
    "}\n"
    "float4 PSMain(VSOut i) : SV_Target {\n"
    "  float2 d = i.uv * 2.0 - 1.0;\n"
    "  float a = saturate(1.15 - dot(d, d)) * color.a;\n"
    "  return float4(color.rgb * a, a);\n"
    "}\n";

static bool CreateScenePipeline() {
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.Num32BitValues = 12;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
    rs_desc.NumParameters = 1;
    rs_desc.pParameters = &param;
    ID3DBlob* rs_blob = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, nullptr))) return false;
    HRESULT hr = g_device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                               IID_PPV_ARGS(&g_scene_root_sig));
    rs_blob->Release();
    if (FAILED(hr)) return false;

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (FAILED(D3DCompile(kSceneShader, strlen(kSceneShader), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0,
                          &vs, nullptr)))
        return false;
    if (FAILED(D3DCompile(kSceneShader, strlen(kSceneShader), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0,
                          &ps, nullptr))) {
        vs->Release();
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = g_scene_root_sig;
    pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = 0xFFFFFFFFu;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.SampleDesc.Count = 1;
    pd.NodeMask = 1;
    hr = g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_scene_pso));
    vs->Release();
    ps->Release();
    return SUCCEEDED(hr);
}

static void DrawScene(float time, float width, float height) {
    static const float kColors[kBlobCount][3] = {
        { 0.95f, 0.30f, 0.35f }, { 0.25f, 0.80f, 0.95f }, { 0.98f, 0.75f, 0.20f }, { 0.45f, 0.95f, 0.45f },
        { 0.80f, 0.40f, 0.95f }, { 0.98f, 0.55f, 0.25f }, { 0.35f, 0.55f, 0.98f },
    };
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, width, height, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)width, (LONG)height };
    g_cmd_list->RSSetViewports(1, &vp);
    g_cmd_list->RSSetScissorRects(1, &sc);
    g_cmd_list->SetGraphicsRootSignature(g_scene_root_sig);
    g_cmd_list->SetPipelineState(g_scene_pso);
    g_cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    for (int i = 0; i < kBlobCount; i++) {
        float fi = (float)i;
        float cx = width * (0.5f + 0.4f * sinf(time * (0.35f + 0.07f * fi) + fi * 1.7f));
        float cy = height * (0.5f + 0.4f * cosf(time * (0.27f + 0.05f * fi) + fi * 0.9f));
        float r = 90.0f + 60.0f * sinf(time * 0.5f + fi);
        float c[12] = { cx - r, cy - r, cx + r, cy + r, kColors[i][0], kColors[i][1], kColors[i][2], 0.85f,
                        width, height, 0.0f, 0.0f };
        g_cmd_list->SetGraphicsRoot32BitConstants(0, 12, c, 0);
        g_cmd_list->DrawInstanced(4, 1, 0, 0);
    }
}

static bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = kBackBuffers;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Scaling = DXGI_SCALING_STRETCH;

#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif
    if (D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)) != S_OK) return false;
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (debug) {
        if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&g_info_queue)))) {
            D3D12_MESSAGE_SEVERITY deny[] = { D3D12_MESSAGE_SEVERITY_INFO, D3D12_MESSAGE_SEVERITY_MESSAGE };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumSeverities = 2;
            filter.DenyList.pSeverityList = deny;
            g_info_queue->PushStorageFilter(&filter);
        }
        debug->Release();
    }
#endif
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = kBackBuffers;
        desc.NodeMask = 1;
        if (g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_rtv_heap)) != S_OK) return false;
        SIZE_T inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < kBackBuffers; i++) {
            g_back_buffer_rtv[i] = h;
            h.ptr += inc;
        }
    }
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = kSrvHeapSize;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_srv_heap)) != S_OK) return false;
        g_srv_alloc.Create(g_device, g_srv_heap, kSrvHeapSize - kFxSrvCount);
    }
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.NodeMask = 1;
        if (g_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_queue)) != S_OK) return false;
    }
    for (int i = 0; i < kFramesInFlight; i++)
        if (g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&g_frame_context[i].allocator)) != S_OK)
            return false;
    if (g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frame_context[0].allocator, nullptr,
                                    IID_PPV_ARGS(&g_cmd_list)) != S_OK ||
        g_cmd_list->Close() != S_OK)
        return false;
    if (g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK) return false;
    g_fence_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!g_fence_event) return false;
    {
        IDXGIFactory4* factory = nullptr;
        IDXGISwapChain1* swap_chain1 = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&factory)) != S_OK) return false;
        if (factory->CreateSwapChainForHwnd(g_queue, hwnd, &sd, nullptr, nullptr, &swap_chain1) != S_OK) return false;
        if (swap_chain1->QueryInterface(IID_PPV_ARGS(&g_swap_chain)) != S_OK) return false;
        swap_chain1->Release();
        factory->Release();
        g_swap_chain->SetMaximumFrameLatency(kBackBuffers);
        g_swap_chain_waitable = g_swap_chain->GetFrameLatencyWaitableObject();
    }
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_scene_pso) { g_scene_pso->Release(); g_scene_pso = nullptr; }
    if (g_scene_root_sig) { g_scene_root_sig->Release(); g_scene_root_sig = nullptr; }
    if (g_swap_chain) { g_swap_chain->SetFullscreenState(false, nullptr); g_swap_chain->Release(); g_swap_chain = nullptr; }
    if (g_swap_chain_waitable) { CloseHandle(g_swap_chain_waitable); g_swap_chain_waitable = nullptr; }
    for (int i = 0; i < kFramesInFlight; i++)
        if (g_frame_context[i].allocator) { g_frame_context[i].allocator->Release(); g_frame_context[i].allocator = nullptr; }
    if (g_queue) { g_queue->Release(); g_queue = nullptr; }
    if (g_cmd_list) { g_cmd_list->Release(); g_cmd_list = nullptr; }
    if (g_rtv_heap) { g_rtv_heap->Release(); g_rtv_heap = nullptr; }
    if (g_srv_heap) { g_srv_heap->Release(); g_srv_heap = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fence_event) { CloseHandle(g_fence_event); g_fence_event = nullptr; }
#ifdef DX12_ENABLE_DEBUG_LAYER
    if (g_info_queue) {
        UINT64 count = g_info_queue->GetNumStoredMessages();
        printf("d3d12 debug layer: %llu warning/error message(s)\n", (unsigned long long)count);
        for (UINT64 i = 0; i < count && i < 50; i++) {
            SIZE_T len = 0;
            g_info_queue->GetMessage(i, nullptr, &len);
            D3D12_MESSAGE* m = (D3D12_MESSAGE*)malloc(len);
            if (m && SUCCEEDED(g_info_queue->GetMessage(i, m, &len)))
                printf("  [sev %d id %d] %s\n", (int)m->Severity, (int)m->ID, m->pDescription);
            free(m);
        }
        g_info_queue->Release();
        g_info_queue = nullptr;
    }
#endif
    if (g_device) { g_device->Release(); g_device = nullptr; }
#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* dxgi_debug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug)))) {
        dxgi_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        dxgi_debug->Release();
    }
#endif
}

static void CreateRenderTarget() {
    for (int i = 0; i < kBackBuffers; i++) {
        ID3D12Resource* bb = nullptr;
        g_swap_chain->GetBuffer((UINT)i, IID_PPV_ARGS(&bb));
        g_device->CreateRenderTargetView(bb, nullptr, g_back_buffer_rtv[i]);
        g_back_buffer[i] = bb;
    }
}

static void CleanupRenderTarget() {
    WaitForLastSubmittedFrame();
    for (int i = 0; i < kBackBuffers; i++)
        if (g_back_buffer[i]) { g_back_buffer[i]->Release(); g_back_buffer[i] = nullptr; }
}

static void WaitForLastSubmittedFrame() {
    FrameContext* frame_ctx = &g_frame_context[g_frame_index % kFramesInFlight];
    UINT64 fence_value = frame_ctx->fence_value;
    if (fence_value == 0) return;
    frame_ctx->fence_value = 0;
    if (g_fence->GetCompletedValue() >= fence_value) return;
    g_fence->SetEventOnCompletion(fence_value, g_fence_event);
    WaitForSingleObject(g_fence_event, INFINITE);
}

static FrameContext* WaitForNextFrameResources() {
    UINT next = g_frame_index + 1;
    g_frame_index = next;
    HANDLE waitables[] = { g_swap_chain_waitable, nullptr };
    DWORD count = 1;
    FrameContext* frame_ctx = &g_frame_context[next % kFramesInFlight];
    UINT64 fence_value = frame_ctx->fence_value;
    if (fence_value != 0) {
        frame_ctx->fence_value = 0;
        g_fence->SetEventOnCompletion(fence_value, g_fence_event);
        waitables[1] = g_fence_event;
        count = 2;
    }
    WaitForMultipleObjects(count, waitables, TRUE, INFINITE);
    return frame_ctx;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_device != nullptr && wparam != SIZE_MINIMIZED) {
            WaitForLastSubmittedFrame();
            CleanupRenderTarget();
            g_swap_chain->ResizeBuffers(0, (UINT)LOWORD(lparam), (UINT)HIWORD(lparam), DXGI_FORMAT_UNKNOWN,
                                        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}
