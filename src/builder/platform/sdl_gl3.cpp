#ifdef IMFORGE_BUILDER_BACKEND_SDL3_GL3

#include <cstdint>
#include <cstdio>

#include "platform.h"

#include <SDL3/SDL.h>

#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace imforge::builder {

namespace {

void SyncTitleBarTheme(SDL_Window* window, int& applied) {
#ifdef _WIN32
    const ImVec4& bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    const int dark = (bg.x * 0.299f + bg.y * 0.587f + bg.z * 0.114f) < 0.5f ? 1 : 0;
    if (dark == applied || !window) return;
    applied = dark;
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    using SetAttrFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static SetAttrFn set_attr = dwm ? (SetAttrFn)(void*)GetProcAddress(dwm, "DwmSetWindowAttribute") : nullptr;
    if (!hwnd || !set_attr) return;
    BOOL v = dark ? TRUE : FALSE;
    if (FAILED(set_attr(hwnd, 20, &v, sizeof(v)))) set_attr(hwnd, 19, &v, sizeof(v));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    (void)window;
    (void)applied;
#endif
}

typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef unsigned int GLbitfield_;
typedef float GLclampf_;
typedef void GLvoid_;

constexpr GLenum_ kGL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum_ kGL_TEXTURE_2D = 0x0DE1;
constexpr GLenum_ kGL_RGBA = 0x1908;
constexpr GLenum_ kGL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum_ kGL_LINEAR = 0x2601;
constexpr GLenum_ kGL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum_ kGL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum_ kGL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum_ kGL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum_ kGL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum_ kGL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr GLenum_ kGL_UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum_ kGL_PACK_ALIGNMENT = 0x0D05;

struct GlFns {
    void (*Viewport)(GLint_, GLint_, GLsizei_, GLsizei_) = nullptr;
    void (*ClearColor)(GLclampf_, GLclampf_, GLclampf_, GLclampf_) = nullptr;
    void (*Clear)(GLbitfield_) = nullptr;
    void (*GenTextures)(GLsizei_, GLuint_*) = nullptr;
    void (*DeleteTextures)(GLsizei_, const GLuint_*) = nullptr;
    void (*BindTexture)(GLenum_, GLuint_) = nullptr;
    void (*TexParameteri)(GLenum_, GLenum_, GLint_) = nullptr;
    void (*PixelStorei)(GLenum_, GLint_) = nullptr;
    void (*TexImage2D)(GLenum_, GLint_, GLint_, GLsizei_, GLsizei_, GLint_, GLenum_, GLenum_, const GLvoid_*) = nullptr;
    void (*ReadPixels)(GLint_, GLint_, GLsizei_, GLsizei_, GLenum_, GLenum_, GLvoid_*) = nullptr;

    template <class T>
    static void Load(T& fn, const char* name) {
        fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    }
    bool LoadAll() {
        Load(Viewport, "glViewport");
        Load(ClearColor, "glClearColor");
        Load(Clear, "glClear");
        Load(GenTextures, "glGenTextures");
        Load(DeleteTextures, "glDeleteTextures");
        Load(BindTexture, "glBindTexture");
        Load(TexParameteri, "glTexParameteri");
        Load(PixelStorei, "glPixelStorei");
        Load(TexImage2D, "glTexImage2D");
        Load(ReadPixels, "glReadPixels");
        return Viewport && ClearColor && Clear && GenTextures && DeleteTextures && BindTexture && TexParameteri &&
               PixelStorei && TexImage2D;
    }
};

class SdlGl3Platform final : public Platform {
public:
    bool Init(const char* title, int w, int h) override {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
        SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        window_ = SDL_CreateWindow(title, w, h, flags);
        if (!window_) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        gl_ = SDL_GL_CreateContext(window_);
        if (!gl_) {
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
            gl_ = SDL_GL_CreateContext(window_);
            glsl_ = "#version 130";
        }
        if (!gl_) {
            std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
            return false;
        }
        SDL_GL_MakeCurrent(window_, gl_);
        SDL_GL_SetSwapInterval(1);
        if (!gl_fns_.LoadAll()) {
            std::fprintf(stderr, "failed to load OpenGL entry points\n");
            return false;
        }
        SyncTitleBarTheme(window_, title_dark_);
        SDL_ShowWindow(window_);
        if (!ImGui_ImplSDL3_InitForOpenGL(window_, gl_)) return false;
        if (!ImGui_ImplOpenGL3_Init(glsl_)) return false;
        return true;
    }

    bool PollEvents(bool& quit) override {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) quit = true;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window_)) quit = true;
        }
        if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            return false;
        }
        return true;
    }

    void BeginFrame() override {
        SyncTitleBarTheme(window_, title_dark_);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    void EndFrame(const ImVec4& clear) override {
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        gl_fns_.Viewport(0, 0, (GLsizei_)io.DisplaySize.x, (GLsizei_)io.DisplaySize.y);
        gl_fns_.ClearColor(clear.x * clear.w, clear.y * clear.w, clear.z * clear.w, clear.w);
        gl_fns_.Clear(kGL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (capture_requested_) ReadBackBuffer();
#ifdef IMGUI_HAS_DOCK
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            SDL_Window* backup_window = SDL_GL_GetCurrentWindow();
            SDL_GLContext backup_ctx = SDL_GL_GetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            SDL_GL_MakeCurrent(backup_window, backup_ctx);
        }
#endif
        SDL_GL_SwapWindow(window_);
    }

    void Shutdown() override {
        if (gl_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            SDL_GL_DestroyContext(gl_);
            gl_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

    float DpiScale() override {
        if (!window_) return 1.0f;
        float s = SDL_GetWindowDisplayScale(window_);
        return s > 0.1f ? s : 1.0f;
    }

    ImTextureID CreateTextureRGBA(const unsigned char* pixels, int w, int h) override {
        GLuint_ tex = 0;
        gl_fns_.GenTextures(1, &tex);
        gl_fns_.BindTexture(kGL_TEXTURE_2D, tex);
        gl_fns_.TexParameteri(kGL_TEXTURE_2D, kGL_TEXTURE_MIN_FILTER, (GLint_)kGL_LINEAR);
        gl_fns_.TexParameteri(kGL_TEXTURE_2D, kGL_TEXTURE_MAG_FILTER, (GLint_)kGL_LINEAR);
        gl_fns_.TexParameteri(kGL_TEXTURE_2D, kGL_TEXTURE_WRAP_S, (GLint_)kGL_CLAMP_TO_EDGE);
        gl_fns_.TexParameteri(kGL_TEXTURE_2D, kGL_TEXTURE_WRAP_T, (GLint_)kGL_CLAMP_TO_EDGE);
        gl_fns_.PixelStorei(kGL_UNPACK_ROW_LENGTH, 0);
        gl_fns_.PixelStorei(kGL_UNPACK_ALIGNMENT, 1);
        gl_fns_.TexImage2D(kGL_TEXTURE_2D, 0, (GLint_)kGL_RGBA, w, h, 0, kGL_RGBA, kGL_UNSIGNED_BYTE, pixels);
        return (ImTextureID)(intptr_t)tex;
    }

    void DestroyTexture(ImTextureID tex) override {
        GLuint_ t = (GLuint_)(intptr_t)tex;
        if (t) gl_fns_.DeleteTextures(1, &t);
    }

    void* GetProcAddress(const char* name) override { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }
    const char* Name() const override { return "SDL3 + OpenGL3"; }
    void SetTitle(const char* title) override {
        if (window_) SDL_SetWindowTitle(window_, title);
    }
    void RebuildFontAtlas() override {
#if IMGUI_VERSION_NUM < 19200
        ImGui_ImplOpenGL3_DestroyFontsTexture();
        ImGui_ImplOpenGL3_CreateFontsTexture();
#endif
    }
    void GetWindowSize(int& w, int& h) override {
        w = h = 0;
        if (window_) SDL_GetWindowSize(window_, &w, &h);
    }
    bool IsMaximized() override { return window_ && (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED); }
    void SetMaximized(bool on) override {
        if (!window_) return;
        if (on) SDL_MaximizeWindow(window_);
        else SDL_RestoreWindow(window_);
    }
    bool RequestCapture() override {
        if (!gl_fns_.ReadPixels || !window_) return false;
        capture_requested_ = true;
        capture_ready_ = false;
        return true;
    }
    bool FetchCapture(int& w, int& h, std::vector<unsigned char>& rgba) override {
        if (!capture_ready_) return false;
        capture_ready_ = false;
        w = capture_w_;
        h = capture_h_;
        rgba.swap(capture_);
        capture_.clear();
        return true;
    }

private:
    void ReadBackBuffer() {
        capture_requested_ = false;
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w <= 0 || h <= 0) return;
        std::vector<unsigned char> px((size_t)w * (size_t)h * 4);
        gl_fns_.PixelStorei(kGL_PACK_ALIGNMENT, 1);
        gl_fns_.ReadPixels(0, 0, w, h, kGL_RGBA, kGL_UNSIGNED_BYTE, px.data());
        const size_t stride = (size_t)w * 4;
        capture_.resize(px.size());
        for (int y = 0; y < h; ++y) {
            unsigned char* dst = capture_.data() + stride * (size_t)y;
            const unsigned char* src = px.data() + stride * (size_t)(h - 1 - y);
            for (size_t i = 0; i < stride; i += 4) {
                dst[i] = src[i];
                dst[i + 1] = src[i + 1];
                dst[i + 2] = src[i + 2];
                dst[i + 3] = 255;
            }
        }
        capture_w_ = w;
        capture_h_ = h;
        capture_ready_ = true;
    }

    int title_dark_ = -1;
    bool capture_requested_ = false, capture_ready_ = false;
    int capture_w_ = 0, capture_h_ = 0;
    std::vector<unsigned char> capture_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    const char* glsl_ = "#version 150";
    GlFns gl_fns_;
};

}  // namespace

std::unique_ptr<Platform> CreatePlatform() { return std::unique_ptr<Platform>(new SdlGl3Platform()); }

}  // namespace imforge::builder

#endif
