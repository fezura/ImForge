// imb::fx blur on SDL3 + OpenGL 3, laid out like imgui's example_sdl3_opengl3.
// Build it like that example plus -I<ImForge>/include. No GL loader needed, imb::fx::gl::Fn()
// has the entry points once imb::fx::Init() has run.
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "imforge/fx/imb_fx_gl3.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>

namespace {

namespace gl = imb::fx::gl;

struct Background {
    gl::GLuint prog = 0;
    gl::GLuint vao = 0;
    gl::GLuint vbo = 0;
    gl::GLint loc_center = -1;
    gl::GLint loc_half = -1;
    gl::GLint loc_angle = -1;
    gl::GLint loc_color = -1;
};

const char* kBgVertex =
    "uniform vec2 u_center;\n"
    "uniform vec2 u_half;\n"
    "uniform float u_angle;\n"
    "in vec2 a_pos;\n"
    "out vec2 v_local;\n"
    "void main() {\n"
    "    v_local = a_pos * 2.0 - 1.0;\n"
    "    float s = sin(u_angle), c = cos(u_angle);\n"
    "    vec2 r = vec2(v_local.x * c - v_local.y * s, v_local.x * s + v_local.y * c);\n"
    "    gl_Position = vec4(u_center + r * u_half, 0.0, 1.0);\n"
    "}\n";

const char* kBgFragment =
    "uniform vec4 u_color;\n"
    "in vec2 v_local;\n"
    "out vec4 o_col;\n"
    "void main() {\n"
    "    float d = max(abs(v_local.x), abs(v_local.y));\n"
    "    float edge = 1.0 - smoothstep(0.82, 1.0, d);\n"
    "    float stripes = 0.85 + 0.15 * step(0.5, fract(v_local.x * 4.0));\n"
    "    o_col = vec4(u_color.rgb * stripes, u_color.a * edge);\n"
    "}\n";

gl::GLuint CompileBgShader(gl::GLenum type, const char* version, bool es, const char* body) {
    gl::Functions& g = gl::Fn();
    const char* src[4] = {version, "\n", es ? "precision highp float;\n" : "", body};
    gl::GLuint sh = g.CreateShader(type);
    g.ShaderSource(sh, 4, src, nullptr);
    g.CompileShader(sh);
    gl::GLint ok = 0;
    g.GetShaderiv(sh, gl::kCompileStatus, &ok);
    if (!ok) {
        char log[512] = {};
        g.GetShaderInfoLog(sh, (gl::GLsizei)sizeof(log), nullptr, log);
        printf("background shader: %s\n", log);
        g.DeleteShader(sh);
        return 0;
    }
    return sh;
}

bool CreateBackground(Background& bg, const char* version, bool es) {
    gl::Functions& g = gl::Fn();
    gl::GLuint vs = CompileBgShader(gl::kVertexShader, version, es, kBgVertex);
    gl::GLuint fs = CompileBgShader(gl::kFragmentShader, version, es, kBgFragment);
    if (!vs || !fs) return false;
    bg.prog = g.CreateProgram();
    g.AttachShader(bg.prog, vs);
    g.AttachShader(bg.prog, fs);
    g.BindAttribLocation(bg.prog, 0, "a_pos");
    g.LinkProgram(bg.prog);
    g.DeleteShader(vs);
    g.DeleteShader(fs);
    gl::GLint ok = 0;
    g.GetProgramiv(bg.prog, gl::kLinkStatus, &ok);
    if (!ok) return false;
    bg.loc_center = g.GetUniformLocation(bg.prog, "u_center");
    bg.loc_half = g.GetUniformLocation(bg.prog, "u_half");
    bg.loc_angle = g.GetUniformLocation(bg.prog, "u_angle");
    bg.loc_color = g.GetUniformLocation(bg.prog, "u_color");
    const float quad[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    g.GenBuffers(1, &bg.vbo);
    g.BindBuffer(gl::kArrayBuffer, bg.vbo);
    g.BufferData(gl::kArrayBuffer, (gl::GLsizeiptr)sizeof(quad), quad, gl::kStaticDraw);
    g.GenVertexArrays(1, &bg.vao);
    g.BindVertexArray(bg.vao);
    g.EnableVertexAttribArray(0);
    g.VertexAttribPointer(0, 2, gl::kFloat, gl::kFalse, 0, nullptr);
    g.BindVertexArray(0);
    return true;
}

void DestroyBackground(Background& bg) {
    gl::Functions& g = gl::Fn();
    if (bg.prog) g.DeleteProgram(bg.prog);
    if (bg.vao) g.DeleteVertexArrays(1, &bg.vao);
    if (bg.vbo) g.DeleteBuffers(1, &bg.vbo);
    bg = Background();
}

void DrawBackground(const Background& bg, float t, int fb_w, int fb_h) {
    gl::Functions& g = gl::Fn();
    g.Viewport(0, 0, fb_w, fb_h);
    g.Disable(gl::kScissorTest);
    g.ClearColor(0.06f, 0.07f, 0.11f, 1.0f);
    g.Clear(gl::kColorBufferBit);
    if (!bg.prog) return;
    g.Enable(gl::kBlend);
    g.BlendEquation(gl::kFuncAdd);
    g.BlendFuncSeparate(gl::kSrcAlpha, gl::kOneMinusSrcAlpha, gl::kOne, gl::kOneMinusSrcAlpha);
    g.Disable(gl::kCullFace);
    g.Disable(gl::kDepthTest);
    g.UseProgram(bg.prog);
    g.BindVertexArray(bg.vao);
    const float aspect = fb_h > 0 ? (float)fb_w / (float)fb_h : 1.0f;
    for (int i = 0; i < 14; i++) {
        const float fi = (float)i;
        const float speed = 0.25f + 0.07f * fi;
        const float cx = sinf(t * speed + fi * 1.7f) * 0.85f;
        const float cy = cosf(t * speed * 0.7f + fi * 2.3f) * 0.85f;
        const float size = 0.12f + 0.16f * (0.5f + 0.5f * sinf(fi * 3.1f));
        float cr, cg, cb;
        ImGui::ColorConvertHSVtoRGB(fmodf(fi * 0.083f + t * 0.03f, 1.0f), 0.8f, 1.0f, cr, cg, cb);
        g.Uniform2f(bg.loc_center, cx, cy);
        g.Uniform2f(bg.loc_half, size / aspect, size);
        g.Uniform1f(bg.loc_angle, t * (0.3f + 0.15f * fi) + fi);
        g.Uniform4f(bg.loc_color, cr, cg, cb, 0.9f);
        g.DrawArrays(gl::kTriangleStrip, 0, 4);
    }
    g.BindVertexArray(0);
    g.UseProgram(0);
}

void* GetGlProc(const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); }

}  // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return -1;
    }

#if defined(IMGUI_IMPL_OPENGL_ES3)
    const char* glsl_version = "#version 300 es";
    const bool glsl_es = true;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
    const char* glsl_version = "#version 150";
    const bool glsl_es = false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    const char* glsl_version = "#version 330";
    const bool glsl_es = false;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN |
                          SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("ImForge fx: GL3 blur demo", 1280, 720, window_flags);
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr) {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 12.0f;

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    imb::fx::Gl3InitDesc fx_desc;
    fx_desc.get_proc = &GetGlProc;
    fx_desc.glsl_version = glsl_version;
    if (!imb::fx::Init(fx_desc)) printf("imb::fx::Init failed: %s\n", imb::fx::Gl3GetLastError());

    Background background;
    if (!CreateBackground(background, glsl_version, glsl_es)) printf("background shader failed\n");

    float radius = 12.0f;
    float rounding = -1.0f;
    float saturation = 1.0f;
    float brightness = 1.0f;
    float noise = 0.25f;
    ImVec4 tint(1.0f, 1.0f, 1.0f, 0.9f);
    bool show_demo = false;
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (show_demo) ImGui::ShowDemoWindow(&show_demo);

        ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.14f, 0.30f));
        const ImGuiWindowFlags glass_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::Begin("imb::fx glass", nullptr, glass_flags);
        {
            imb::fx::BlurParams p;
            p.radius = radius;
            p.rounding = rounding < 0.0f ? ImGui::GetStyle().WindowRounding : rounding;
            p.tint = ImGui::ColorConvertFloat4ToU32(tint);
            p.saturation = saturation;
            p.brightness = brightness;
            p.noise = noise;
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            const ImVec2 max(pos.x + size.x, pos.y + size.y);
            imb::fx::Blur(ImGui::GetWindowDrawList(), pos, max, p);
        }
        ImGui::TextUnformatted("imb::fx::Blur(GetWindowDrawList(), ...)");
        ImGui::Text("fx available: %s", imb::fx::IsAvailable() ? "yes" : "no");
        ImGui::Separator();
        ImGui::SliderFloat("radius", &radius, 0.0f, 64.0f, "%.1f px");
        ImGui::SliderFloat("rounding", &rounding, -1.0f, 48.0f,
                           rounding < 0.0f ? "style" : "%.0f px");
        ImGui::ColorEdit4("tint", &tint.x);
        ImGui::SliderFloat("saturation", &saturation, 0.0f, 2.0f);
        ImGui::SliderFloat("brightness", &brightness, 0.0f, 2.0f);
        ImGui::SliderFloat("noise", &noise, 0.0f, 1.0f);
        ImGui::Checkbox("ImGui demo window", &show_demo);
        ImGui::Text("%.1f ms/frame (%.0f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();

        ImGui::SetNextWindowPos(ImVec2(520.0f, 120.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("BlurCurrentWindow", nullptr, glass_flags);
        imb::fx::BlurCurrentWindow(12.0f);
        ImGui::TextUnformatted("imb::fx::BlurCurrentWindow(12.f)");
        ImGui::TextUnformatted("Drag me over the moving quads.");
        ImGui::End();
        ImGui::PopStyleColor();

        int fb_w = 0, fb_h = 0;
        SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
        DrawBackground(background, (float)SDL_GetTicks() * 0.001f, fb_w, fb_h);

        imb::fx::FrameDesc fd;
        fd.width = fb_w;
        fd.height = fb_h;
        fd.fbo = 0;
        imb::fx::PrepareFrame(fd);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    DestroyBackground(background);
    imb::fx::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
