// imb::fx blur on Win32 + DX9: animated background, one ImGui window blurring what's behind it.
// Based on imgui's example_win32_directx9.
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "imforge/fx/imb_fx_dx9.h"
#include <d3d9.h>
#include <math.h>
#include <tchar.h>

static LPDIRECT3D9 g_pD3D = nullptr;
static LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr;
static bool g_DeviceLost = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS g_d3dpp = {};

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct BgVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};

static void DrawBackground(float t, float w, float h) {
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(18, 20, 28),
                        1.0f, 0);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetTexture(0, nullptr);
    g_pd3dDevice->SetPixelShader(nullptr);
    g_pd3dDevice->SetVertexShader(nullptr);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    const D3DCOLOR colors[6] = {
        D3DCOLOR_ARGB(230, 255, 90, 60),  D3DCOLOR_ARGB(230, 70, 200, 120),
        D3DCOLOR_ARGB(230, 60, 140, 255), D3DCOLOR_ARGB(230, 250, 200, 40),
        D3DCOLOR_ARGB(230, 220, 60, 200), D3DCOLOR_ARGB(230, 40, 220, 230),
    };
    for (int i = 0; i < 6; i++) {
        const float ph = (float)i * 1.047f;
        const float cx = w * (0.5f + 0.38f * cosf(t * 0.5f + ph));
        const float cy = h * (0.5f + 0.35f * sinf(t * 0.7f + ph * 1.3f));
        const float sz = 70.0f + 40.0f * sinf(t * 1.1f + ph);
        const float a = t * 0.8f + ph;
        const float c = cosf(a) * sz, s = sinf(a) * sz;
        const BgVertex v[4] = {
            { cx - c + s, cy - s - c, 0.0f, 1.0f, colors[i] },
            { cx + c + s, cy + s - c, 0.0f, 1.0f, colors[i] },
            { cx - c - s, cy - s + c, 0.0f, 1.0f, colors[i] },
            { cx + c - s, cy + s + c, 0.0f, 1.0f, colors[i] },
        };
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BgVertex));
    }
    for (int i = 0; i < 24; i++) {
        const float x = (float)i / 24.0f * w + fmodf(t * 40.0f, w / 24.0f);
        const D3DCOLOR col = D3DCOLOR_ARGB(255, 90, 95, 120);
        const BgVertex v[4] = {
            { x, 0.0f, 0.0f, 1.0f, col }, { x + 2.0f, 0.0f, 0.0f, 1.0f, col },
            { x, h, 0.0f, 1.0f, col },    { x + 2.0f, h, 0.0f, 1.0f, col },
        };
        g_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(BgVertex));
    }
}

int main(int, char**) {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr,
                       nullptr, nullptr, nullptr, L"ImForge fx dx9", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ImForge fx: DX9 blur demo", WS_OVERLAPPEDWINDOW,
                                100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 12.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    imb::fx::Dx9InitDesc fx_desc;
    fx_desc.device = g_pd3dDevice;
    const bool fx_ok = imb::fx::Init(fx_desc);

    float radius = 12.0f;
    float rounding = -1.0f;
    float saturation = 1.0f;
    float brightness = 1.0f;
    float noise = 0.1f;
    ImVec4 tint = ImVec4(0.85f, 0.9f, 1.0f, 1.0f);
    ImVec4 bg = ImVec4(0.08f, 0.09f, 0.12f, 0.25f);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (g_DeviceLost) {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST) {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET) ResetDevice();
            g_DeviceLost = false;
        }

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(80.0f, 80.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::Begin("Blur", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
        imb::fx::BlurParams bp;
        bp.radius = radius;
        bp.rounding = rounding < 0.0f ? ImGui::GetStyle().WindowRounding : rounding;
        bp.tint = ImGui::ColorConvertFloat4ToU32(tint);
        bp.saturation = saturation;
        bp.brightness = brightness;
        bp.noise = noise;
        {
            const ImVec2 p = ImGui::GetWindowPos();
            const ImVec2 sz = ImGui::GetWindowSize();
            imb::fx::Blur(ImGui::GetWindowDrawList(), p, ImVec2(p.x + sz.x, p.y + sz.y), bp);
        }
        ImGui::TextUnformatted("imb::fx DX9 blur");
        ImGui::Separator();
        ImGui::Text("fx available: %s", fx_ok && imb::fx::IsAvailable() ? "yes" : "no");
        ImGui::SliderFloat("radius", &radius, 0.0f, 64.0f, "%.1f px");
        ImGui::SliderFloat("rounding", &rounding, -1.0f, 48.0f, rounding < 0.0f ? "style" : "%.1f");
        ImGui::SliderFloat("saturation", &saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("brightness", &brightness, 0.0f, 2.0f);
        ImGui::SliderFloat("noise", &noise, 0.0f, 1.0f);
        ImGui::ColorEdit4("tint", (float*)&tint);
        ImGui::ColorEdit4("window bg", (float*)&bg);
        ImGui::Text("%.1f FPS", io.Framerate);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SetNextWindowPos(ImVec2(620.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360.0f, 220.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::Begin("Frosted", nullptr, ImGuiWindowFlags_NoTitleBar);
        imb::fx::BlurCurrentWindow(radius);
        ImGui::TextUnformatted("BlurCurrentWindow(radius)");
        ImGui::TextWrapped("Drag me around. The blur samples the back buffer captured by "
                           "imb::fx::PrepareFrame() before ImGui renders.");
        ImGui::End();
        ImGui::PopStyleColor();

        ImGui::EndFrame();
        const float t = (float)ImGui::GetTime();
        if (g_pd3dDevice->BeginScene() >= 0) {
            DrawBackground(t, (float)g_d3dpp.BackBufferWidth, (float)g_d3dpp.BackBufferHeight);
            imb::fx::FrameDesc fd;
            fd.width = (int)g_d3dpp.BackBufferWidth;
            fd.height = (int)g_d3dpp.BackBufferHeight;
            imb::fx::PrepareFrame(fd);
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            g_pd3dDevice->EndScene();
        }
        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST) g_DeviceLost = true;
    }

    imb::fx::Shutdown();
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr) return false;
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
                             D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;
    D3DSURFACE_DESC desc;
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(g_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        if (SUCCEEDED(bb->GetDesc(&desc))) {
            g_d3dpp.BackBufferWidth = desc.Width;
            g_d3dpp.BackBufferHeight = desc.Height;
        }
        bb->Release();
    }
    return true;
}

void CleanupDeviceD3D() {
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pD3D) {
        g_pD3D->Release();
        g_pD3D = nullptr;
    }
}

void ResetDevice() {
    imb::fx::OnDeviceLost();
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL) IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
    imb::fx::OnDeviceReset();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
