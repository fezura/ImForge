// ImForge live-attach example for SDL3 + OpenGL 3, based on imgui's example_sdl3_opengl3.
// Everything ImForge adds is marked "ImForge:".
#define IMFORGE_LIVE
#define IMFORGE_BACKEND_NAME "sdl3+gl3"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "imforge/imforge_client.h"
#include "../common/demo_app.h"
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL3/SDL_opengles2.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#if __has_include("imforge/fx/imb_fx_gl3.h")
#include "imforge/fx/imb_fx_gl3.h"
#define DEMO_HAS_FX 1
#endif

int main(int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

#if defined(IMGUI_IMPL_OPENGL_ES2)
    const char* glsl_version = "#version 100";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(IMGUI_IMPL_OPENGL_ES3)
    const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    const char* glsl_version = "#version 150";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("ImForge demo (SDL3 + OpenGL 3)", 1280, 720, window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return -1;
    }

    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // ImForge: connect to the builder (non-blocking, reconnects automatically).
    const char* url = argc > 1 ? argv[1] : "tcp://127.0.0.1:7777";
    imforge::live::SetAppName("ImForge demo (sdl3+gl3)");
    imforge::live::RegisterBundledPacks();
    imforge::live::Connect(url);
    DemoSetUrl(url);

    // ImForge: project fonts from the builder land in the atlas in PreNewFrame(), this recreates the
    // font texture afterwards (1.92 backends update textures on their own).
#if IMGUI_VERSION_NUM < 19200
    imforge::live::SetFontTextureRebuild([] { ImGui_ImplOpenGL3_DestroyFontsTexture(); ImGui_ImplOpenGL3_CreateFontsTexture(); });
#endif

#ifdef DEMO_HAS_FX
    // ImForge: shader fx (blur behind windows) for this backend.
    imb::fx::Gl3InitDesc fx_desc;
    fx_desc.get_proc = [](const char* name) -> void* { return (void*)SDL_GL_GetProcAddress(name); };
    fx_desc.glsl_version = glsl_version;
    imb::fx::Init(fx_desc);
#endif

    Player player = MakeDemoPlayer();
    Settings cfg = MakeDemoSettings();
    ImVec4 clear_color = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);

    bool done = false;
    while (!done)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
        {
            SDL_Delay(10);
            continue;
        }

        // ImForge: load project fonts the builder sent, before the backend NewFrame locks the atlas.
        imforge::live::PreNewFrame();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // ImForge: animate the sample state, then run the shared demo frame
        // (imforge::live::NewFrame + status window + live panels).
        SimulateGame(player, io.DeltaTime);
        DemoFrame(player, cfg);

        int fb_width = 0, fb_height = 0;
        SDL_GetWindowSizeInPixels(window, &fb_width, &fb_height);
        glViewport(0, 0, fb_width, fb_height);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        // (your scene would render here)
#ifdef DEMO_HAS_FX
        // ImForge: capture the back buffer after the scene, before ImGui is drawn on top.
        imb::fx::FrameDesc fd;
        fd.width = fb_width;
        fd.height = fb_height;
        fd.fbo = 0;
        imb::fx::PrepareFrame(fd);
#endif
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    imforge::live::Disconnect();
#ifdef DEMO_HAS_FX
    imb::fx::Shutdown();
#endif
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
