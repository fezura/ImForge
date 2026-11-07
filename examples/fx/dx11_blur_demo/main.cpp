// imb::fx blur on Win32 + DX11: an animated background with two frosted ImGui windows on top.
// --frames N quits after N frames.
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imforge/fx/imb_fx_dx11.h"

#include <d3d11.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static_assert(IMB_FX_AVAILABLE == 1, "imb_fx_dx11.h must define IMB_FX_AVAILABLE");

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0;
UINT g_resize_h = 0;
bool g_occluded = false;

struct BgVertex {
    float x, y;
    float r, g, b, a;
};

constexpr int kRects = 24;
constexpr int kMaxVerts = (kRects + 1) * 6;

ID3D11VertexShader* g_bg_vs = nullptr;
ID3D11PixelShader* g_bg_ps = nullptr;
ID3D11InputLayout* g_bg_layout = nullptr;
ID3D11Buffer* g_bg_vb = nullptr;
ID3D11BlendState* g_bg_blend = nullptr;
ID3D11RasterizerState* g_bg_rs = nullptr;

const char* kBgShader = R"hlsl(
struct VSIn { float2 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; };
VSOut VSMain(VSIn i) { VSOut o; o.pos = float4(i.pos, 0.0, 1.0); o.col = i.col; return o; }
float4 PSMain(VSOut i) : SV_Target { return i.col; }
)hlsl";

ID3DBlob* CompileBg(const char* entry, const char* target) {
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(kBgShader, strlen(kBgShader), "bg", nullptr, nullptr, entry, target, 0,
                            0, &blob, &err);
    if (err) {
        fprintf(stderr, "%s\n", (const char*)err->GetBufferPointer());
        err->Release();
    }
    if (FAILED(hr)) return nullptr;
    return blob;
}

bool CreateBackground() {
    ID3DBlob* vs = CompileBg("VSMain", "vs_4_0");
    ID3DBlob* ps = CompileBg("PSMain", "ps_4_0");
    if (!vs || !ps) return false;
    bool ok = SUCCEEDED(g_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                                     nullptr, &g_bg_vs));
    ok = ok && SUCCEEDED(g_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                                     nullptr, &g_bg_ps));
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ok = ok && SUCCEEDED(g_device->CreateInputLayout(layout, 2, vs->GetBufferPointer(),
                                                     vs->GetBufferSize(), &g_bg_layout));
    vs->Release();
    ps->Release();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(BgVertex) * kMaxVerts;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ok = ok && SUCCEEDED(g_device->CreateBuffer(&bd, nullptr, &g_bg_vb));
    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ok = ok && SUCCEEDED(g_device->CreateBlendState(&bl, &g_bg_blend));
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    ok = ok && SUCCEEDED(g_device->CreateRasterizerState(&rd, &g_bg_rs));
    return ok;
}

template <typename T> void Release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void DestroyBackground() {
    Release(g_bg_rs);
    Release(g_bg_blend);
    Release(g_bg_vb);
    Release(g_bg_layout);
    Release(g_bg_ps);
    Release(g_bg_vs);
}

void PushQuad(BgVertex* v, int& n, float x0, float y0, float x1, float y1, const ImVec4& c00,
              const ImVec4& c10, const ImVec4& c11, const ImVec4& c01) {
    const BgVertex a = {x0, y0, c00.x, c00.y, c00.z, c00.w};
    const BgVertex b = {x1, y0, c10.x, c10.y, c10.z, c10.w};
    const BgVertex c = {x1, y1, c11.x, c11.y, c11.z, c11.w};
    const BgVertex d = {x0, y1, c01.x, c01.y, c01.z, c01.w};
    v[n++] = a;
    v[n++] = b;
    v[n++] = d;
    v[n++] = b;
    v[n++] = c;
    v[n++] = d;
}

ImVec4 Hsv(float h, float s, float v, float a) {
    ImVec4 c;
    h = h - floorf(h);
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    c.w = a;
    return c;
}

void DrawBackground(float t, int width, int height) {
    BgVertex verts[kMaxVerts];
    int n = 0;
    PushQuad(verts, n, -1.0f, -1.0f, 1.0f, 1.0f, Hsv(0.62f + t * 0.01f, 0.6f, 0.35f, 1.0f),
             Hsv(0.80f + t * 0.01f, 0.6f, 0.30f, 1.0f), Hsv(0.95f + t * 0.01f, 0.5f, 0.25f, 1.0f),
             Hsv(0.55f + t * 0.01f, 0.7f, 0.20f, 1.0f));
    float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    for (int i = 0; i < kRects; i++) {
        float ph = (float)i * 0.731f;
        float sp = 0.25f + 0.045f * (float)(i % 7);
        float cx = sinf(t * sp + ph) * 0.85f;
        float cy = cosf(t * sp * 0.8f + ph * 1.7f) * 0.85f;
        float hw = (0.06f + 0.05f * (float)((i * 7) % 5) / 4.0f) / aspect;
        float hh = 0.08f + 0.07f * (float)((i * 3) % 4) / 3.0f;
        ImVec4 c = Hsv((float)i / (float)kRects + t * 0.03f, 0.85f, 1.0f, 0.85f);
        PushQuad(verts, n, cx - hw, cy - hh, cx + hw, cy + hh, c, c, c, c);
    }
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(g_context->Map(g_bg_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    memcpy(m.pData, verts, sizeof(BgVertex) * (size_t)n);
    g_context->Unmap(g_bg_vb, 0);

    D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    g_context->RSSetViewports(1, &vp);
    g_context->RSSetState(g_bg_rs);
    const float factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    g_context->OMSetBlendState(g_bg_blend, factor, 0xffffffff);
    UINT stride = sizeof(BgVertex);
    UINT offset = 0;
    g_context->IASetInputLayout(g_bg_layout);
    g_context->IASetVertexBuffers(0, 1, &g_bg_vb, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_bg_vs, nullptr, 0);
    g_context->PSSetShader(g_bg_ps, nullptr, 0);
    g_context->Draw((UINT)n, 0);
}

void CreateRenderTarget() {
    ID3D11Texture2D* back_buffer = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer) {
        g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
        back_buffer->Release();
    }
}

void DestroyRenderTarget() { Release(g_rtv); }

bool CreateDevice(HWND hwnd, bool debug_layer) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    UINT flags = debug_layer ? D3D11_CREATE_DEVICE_DEBUG : 0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                               levels, 2, D3D11_SDK_VERSION, &sd, &g_swap_chain,
                                               &g_device, &got, &g_context);
    if (FAILED(hr) && debug_layer) {
        fprintf(stderr, "debug layer unavailable, retrying without it\n");
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                           2, D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device,
                                           &got, &g_context);
    }
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &sd, &g_swap_chain, &g_device, &got,
                                           &g_context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

int DumpDebugMessages() {
    ID3D11InfoQueue* queue = nullptr;
    if (!g_device || FAILED(g_device->QueryInterface(IID_PPV_ARGS(&queue)))) return 0;
    const UINT64 count = queue->GetNumStoredMessages();
    int printed = 0;
    for (UINT64 i = 0; i < count; i++) {
        SIZE_T len = 0;
        queue->GetMessage(i, nullptr, &len);
        D3D11_MESSAGE* m = (D3D11_MESSAGE*)malloc(len);
        if (m && SUCCEEDED(queue->GetMessage(i, m, &len)) &&
            m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
            fprintf(stderr, "D3D11 [sev %d id %d] %s\n", (int)m->Severity, (int)m->ID,
                    m->pDescription);
            printed++;
        }
        free(m);
    }
    queue->Release();
    return printed;
}

void DestroyDevice() {
    DestroyRenderTarget();
    Release(g_swap_chain);
    Release(g_context);
    Release(g_device);
}

void DrawGlassBorder(float rounding, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), color, rounding, 0, 1.5f);
}

void DrawUi(imb::fx::BlurParams& params, float* tint_rgba) {
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400.0f, 190.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Glass A", nullptr, flags);
    imb::fx::BlurCurrentWindow(12.0f);
    DrawGlassBorder(ImGui::GetStyle().WindowRounding, IM_COL32(255, 255, 255, 110));
    ImGui::TextUnformatted("imb::fx::BlurCurrentWindow(12.f)");
    ImGui::Separator();
    ImGui::TextWrapped("The window background is fully transparent; what you see behind the "
                       "text is the DX11 back buffer captured before ImGui rendered, blurred "
                       "with a separable Gaussian at a reduced resolution.");
    ImGui::Text("fx available: %s", imb::fx::IsAvailable() ? "yes" : "no");
    ImGui::Text("%.1f ms/frame (%.0f fps)", 1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(520.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 330.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Glass B", nullptr, flags);
    {
        ImVec2 p = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        imb::fx::Blur(ImGui::GetWindowDrawList(), p, ImVec2(p.x + sz.x, p.y + sz.y), params);
    }
    DrawGlassBorder(params.rounding, IM_COL32(255, 255, 255, 140));
    ImGui::TextUnformatted("imb::fx::Blur(dl, min, max, BlurParams)");
    ImGui::Separator();
    ImGui::SliderFloat("radius", &params.radius, 0.0f, 64.0f, "%.1f px");
    ImGui::SliderFloat("rounding", &params.rounding, 0.0f, 48.0f, "%.1f px");
    if (ImGui::ColorEdit4("tint", tint_rgba))
        params.tint = ImGui::ColorConvertFloat4ToU32(
            ImVec4(tint_rgba[0], tint_rgba[1], tint_rgba[2], tint_rgba[3]));
    ImGui::SliderFloat("saturation", &params.saturation, 0.0f, 2.0f);
    ImGui::SliderFloat("brightness", &params.brightness, 0.0f, 2.0f);
    ImGui::SliderFloat("noise", &params.noise, 0.0f, 1.0f);
    if (ImGui::Button("Reset")) {
        params = imb::fx::BlurParams();
        params.radius = 18.0f;
        params.rounding = 16.0f;
        params.tint = IM_COL32(225, 232, 255, 235);
        ImVec4 t = ImGui::ColorConvertU32ToFloat4(params.tint);
        tint_rgba[0] = t.x;
        tint_rgba[1] = t.y;
        tint_rgba[2] = t.z;
        tint_rgba[3] = t.w;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("drag the windows over the moving rects");
    ImGui::End();

    ImGui::PopStyleColor();

    ImGui::SetNextWindowPos(ImVec2(60.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Opaque window");
    ImGui::TextUnformatted("A regular window, for comparison.");
    ImGui::End();
}

}  // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) return 0;
        g_resize_w = (UINT)LOWORD(lparam);
        g_resize_h = (UINT)HIWORD(lparam);
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

int main(int argc, char** argv) {
    int max_frames = -1;
    bool debug_layer = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = atoi(argv[++i]);
        if (strcmp(argv[i], "--debug") == 0) debug_layer = true;
    }

    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr,
                      nullptr, nullptr, nullptr, L"ImForgeFxDx11Demo", nullptr};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ImForge fx: DX11 blur demo",
                                WS_OVERLAPPEDWINDOW, 100, 100, 1100, 720, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!CreateDevice(hwnd, debug_layer)) {
        fprintf(stderr, "D3D11 device creation failed\n");
        DestroyDevice();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    if (!CreateBackground()) {
        fprintf(stderr, "background shader creation failed\n");
        DestroyBackground();
        DestroyDevice();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 12.0f;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    imb::fx::Dx11InitDesc fx_desc;
    fx_desc.device = g_device;
    fx_desc.context = g_context;
    const bool fx_ok = imb::fx::Init(fx_desc);
    if (!fx_ok) fprintf(stderr, "imb::fx::Init failed; windows will render without blur\n");

    imb::fx::BlurParams params;
    params.radius = 18.0f;
    params.rounding = 16.0f;
    params.tint = IM_COL32(225, 232, 255, 235);
    float tint_rgba[4];
    {
        ImVec4 t = ImGui::ColorConvertU32ToFloat4(params.tint);
        tint_rgba[0] = t.x;
        tint_rgba[1] = t.y;
        tint_rgba[2] = t.z;
        tint_rgba[3] = t.w;
    }

    int frames = 0;
    float time = 0.0f;
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_occluded && g_swap_chain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_occluded = false;
        if (g_resize_w != 0 && g_resize_h != 0) {
            DestroyRenderTarget();
            g_swap_chain->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUi(params, tint_rgba);
        ImGui::Render();

        time += ImGui::GetIO().DeltaTime;
        const int width = (int)ImGui::GetIO().DisplaySize.x;
        const int height = (int)ImGui::GetIO().DisplaySize.y;
        const float clear_color[4] = {0.05f, 0.05f, 0.08f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear_color);
        DrawBackground(time, width, height);

        imb::fx::FrameDesc fd;
        fd.back_buffer_rtv = g_rtv;
        fd.width = width;
        fd.height = height;
        imb::fx::PrepareFrame(fd);

        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_swap_chain->Present(1, 0);
        g_occluded = (hr == DXGI_STATUS_OCCLUDED);

        frames++;
        if (max_frames >= 0 && frames >= max_frames) ::PostQuitMessage(0);
    }

    imb::fx::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyBackground();
    const int d3d_warnings = debug_layer ? DumpDebugMessages() : 0;
    DestroyDevice();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    printf("IMB_FX_DX11_DEMO_OK frames=%d fx=%d d3d_warnings=%d\n", frames, fx_ok ? 1 : 0,
           d3d_warnings);
    return d3d_warnings == 0 ? 0 : 2;
}
