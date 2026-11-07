#if defined(IMFORGE_BUILDER_BACKEND_WIN32_DX11) && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>

#include <cstdint>
#include <cstdio>

#include "platform.h"

#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace imforge::builder {

namespace {

class WinDx11Platform;
WinDx11Platform* g_platform = nullptr;

class WinDx11Platform final : public Platform {
public:
    bool Init(const char* title, int w, int h) override {
        ImGui_ImplWin32_EnableDpiAwareness();
        wc_ = {sizeof(wc_), CS_CLASSDC, WndProcThunk, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
               L"ImForgeBuilder", nullptr};
        wc_.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc_);
        g_platform = this;
        float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        int ww = (int)(w * scale), wh = (int)(h * scale);
        if (ww > sw) ww = sw;
        if (wh > sh - 80) wh = sh - 80;
        wchar_t wtitle[512];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 512);
        hwnd_ = CreateWindowW(wc_.lpszClassName, wtitle, WS_OVERLAPPEDWINDOW, (sw - ww) / 2, (sh - wh) / 2, ww, wh,
                              nullptr, nullptr, wc_.hInstance, nullptr);
        if (!hwnd_) return false;
        if (!CreateDevice()) {
            CleanupDevice();
            return false;
        }
        ShowWindow(hwnd_, SW_SHOWDEFAULT);
        UpdateWindow(hwnd_);
        if (!ImGui_ImplWin32_Init(hwnd_)) return false;
        if (!ImGui_ImplDX11_Init(device_, context_)) return false;
        return true;
    }

    bool PollEvents(bool& quit) override {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) return false;
        if (occluded_ && swap_chain_->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            return false;
        }
        occluded_ = false;
        if (resize_w_ != 0 && resize_h_ != 0) {
            CleanupRenderTarget();
            swap_chain_->ResizeBuffers(0, resize_w_, resize_h_, DXGI_FORMAT_UNKNOWN, 0);
            resize_w_ = resize_h_ = 0;
            CreateRenderTarget();
        }
        return true;
    }

    void BeginFrame() override {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame(const ImVec4& clear) override {
        ImGui::Render();
        const float c[4] = {clear.x * clear.w, clear.y * clear.w, clear.z * clear.w, clear.w};
        context_->OMSetRenderTargets(1, &rtv_, nullptr);
        context_->ClearRenderTargetView(rtv_, c);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#ifdef IMGUI_HAS_DOCK
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
#endif
        HRESULT hr = swap_chain_->Present(1, 0);
        occluded_ = hr == DXGI_STATUS_OCCLUDED;
    }

    void Shutdown() override {
        if (device_) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
        }
        CleanupDevice();
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        UnregisterClassW(wc_.lpszClassName, wc_.hInstance);
        g_platform = nullptr;
    }

    float DpiScale() override { return hwnd_ ? ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd_) : 1.0f; }

    ImTextureID CreateTextureRGBA(const unsigned char* pixels, int w, int h) override {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = (UINT)w;
        desc.Height = (UINT)h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = pixels;
        sub.SysMemPitch = (UINT)w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(device_->CreateTexture2D(&desc, &sub, &tex))) return (ImTextureID)0;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* view = nullptr;
        HRESULT hr = device_->CreateShaderResourceView(tex, &srv, &view);
        tex->Release();
        if (FAILED(hr)) return (ImTextureID)0;
        return (ImTextureID)(intptr_t)view;
    }

    void DestroyTexture(ImTextureID tex) override {
        if (ID3D11ShaderResourceView* view = (ID3D11ShaderResourceView*)(intptr_t)tex) view->Release();
    }

    void* GetProcAddress(const char*) override { return nullptr; }
    const char* Name() const override { return "Win32 + DirectX 11"; }
    void SetTitle(const char* title) override {
        if (!hwnd_) return;
        wchar_t wtitle[512];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 512);
        SetWindowTextW(hwnd_, wtitle);
    }
    void RebuildFontAtlas() override {
#if IMGUI_VERSION_NUM < 19200
        ImGui_ImplDX11_InvalidateDeviceObjects();
        ImGui_ImplDX11_CreateDeviceObjects();
#endif
    }
    void GetWindowSize(int& w, int& h) override {
        RECT r;
        if (hwnd_ && GetClientRect(hwnd_, &r)) {
            w = r.right - r.left;
            h = r.bottom - r.top;
        } else {
            w = h = 0;
        }
    }
    bool IsMaximized() override { return hwnd_ && IsZoomed(hwnd_); }
    void SetMaximized(bool on) override {
        if (hwnd_) ShowWindow(hwnd_, on ? SW_MAXIMIZE : SW_RESTORE);
    }

    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
        switch (msg) {
        case WM_SIZE:
            if (wParam == SIZE_MINIMIZED) return 0;
            resize_w_ = (UINT)LOWORD(lParam);
            resize_h_ = (UINT)HIWORD(lParam);
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        case WM_DPICHANGED:
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports) {
                const RECT* r = (RECT*)lParam;
                SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

private:
    static LRESULT WINAPI WndProcThunk(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (g_platform) return g_platform->WndProc(hWnd, msg, wParam, lParam);
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool CreateDevice() {
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd_;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        UINT flags = 0;
        D3D_FEATURE_LEVEL level;
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                                   D3D11_SDK_VERSION, &sd, &swap_chain_, &device_, &level, &context_);
        if (hr == DXGI_ERROR_UNSUPPORTED)
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &swap_chain_, &device_, &level, &context_);
        if (FAILED(hr)) return false;
        CreateRenderTarget();
        return true;
    }

    void CleanupDevice() {
        CleanupRenderTarget();
        if (swap_chain_) swap_chain_->Release();
        if (context_) context_->Release();
        if (device_) device_->Release();
        swap_chain_ = nullptr;
        context_ = nullptr;
        device_ = nullptr;
    }

    void CreateRenderTarget() {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
            device_->CreateRenderTargetView(back, nullptr, &rtv_);
            back->Release();
        }
    }

    void CleanupRenderTarget() {
        if (rtv_) {
            rtv_->Release();
            rtv_ = nullptr;
        }
    }

    WNDCLASSEXW wc_ = {};
    HWND hwnd_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* rtv_ = nullptr;
    UINT resize_w_ = 0, resize_h_ = 0;
    bool occluded_ = false;
};

}  // namespace

std::unique_ptr<Platform> CreatePlatform() { return std::unique_ptr<Platform>(new WinDx11Platform()); }

}  // namespace imforge::builder

#endif
