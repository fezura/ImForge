#pragma once
// imb::fx OpenGL backend: GL 3.0+ (core/compat, GLSL 130+) and GL ES 3.0 (GLSL 300 es).
// Doesn't include any system GL headers, entry points come from Gl3InitDesc::get_proc. Use
// SDL_GL_GetProcAddress or glfwGetProcAddress, raw wglGetProcAddress misses the GL 1.1 functions.
//
//   imb::fx::Gl3InitDesc desc;
//   desc.get_proc = [](const char* n) -> void* { return (void*)SDL_GL_GetProcAddress(n); };
//   desc.glsl_version = "#version 330";      // "#version 300 es" on GL ES
//   imb::fx::Init(desc);                     // GL context current, after ImGui::CreateContext()
//   per frame: render scene, imb::fx::PrepareFrame(fd), ImGui::Render(), RenderDrawData
//
// The loaded functions are in imb::fx::gl::Fn() without the "gl" prefix, constants are
// imb::fx::gl::k*, so this coexists with your own GL loader.
#include "imb_fx.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define IMB_FX_AVAILABLE 1
#define IMB_FX_GL3 1

#ifdef _WIN32
#define IMB_FX_GL_APIENTRY __stdcall
#else
#define IMB_FX_GL_APIENTRY
#endif

namespace imb {
namespace fx {

struct Gl3InitDesc {
    void* (*get_proc)(const char* name) = nullptr;
    const char* glsl_version = "#version 330";
};

struct FrameDesc {
    int width = 0;
    int height = 0;
    unsigned fbo = 0;   // may be multisampled, 0 = default framebuffer
};

bool Init(const Gl3InitDesc& desc);
const char* Gl3GetLastError();

namespace gl {

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

enum : GLenum {
    kFalse = 0,
    kZero = 0,
    kOne = 1,
    kTriangleStrip = 0x0005,
    kSrcAlpha = 0x0302,
    kOneMinusSrcAlpha = 0x0303,
    kFrontAndBack = 0x0408,
    kPolygonMode = 0x0B40,
    kCullFace = 0x0B44,
    kDepthTest = 0x0B71,
    kStencilTest = 0x0B90,
    kViewport = 0x0BA2,
    kBlend = 0x0BE2,
    kScissorBox = 0x0C10,
    kScissorTest = 0x0C11,
    kTexture2D = 0x0DE1,
    kUnsignedByte = 0x1401,
    kFloat = 0x1406,
    kRgba = 0x1908,
    kFill = 0x1B02,
    kNearest = 0x2600,
    kLinear = 0x2601,
    kTextureMagFilter = 0x2800,
    kTextureMinFilter = 0x2801,
    kTextureWrapS = 0x2802,
    kTextureWrapT = 0x2803,
    kColorBufferBit = 0x4000,
    kFuncAdd = 0x8006,
    kRgba8 = 0x8058,
    kTextureBinding2D = 0x8069,
    kClampToEdge = 0x812F,
    kTexture0 = 0x84C0,
    kTexture1 = 0x84C1,
    kActiveTexture = 0x84E0,
    kVertexArrayBinding = 0x85B5,
    kArrayBuffer = 0x8892,
    kArrayBufferBinding = 0x8894,
    kStaticDraw = 0x88E4,
    kSamplerBinding = 0x8919,
    kFragmentShader = 0x8B30,
    kVertexShader = 0x8B31,
    kCompileStatus = 0x8B81,
    kLinkStatus = 0x8B82,
    kCurrentProgram = 0x8B8D,
    kDrawFramebufferBinding = 0x8CA6,
    kReadFramebuffer = 0x8CA8,
    kDrawFramebuffer = 0x8CA9,
    kReadFramebufferBinding = 0x8CAA,
    kFramebufferComplete = 0x8CD5,
    kColorAttachment0 = 0x8CE0,
    kFramebuffer = 0x8D40,
};

#define IMB_FX_GL_FUNCS(X)                                                                        \
    X(void, Enable, (GLenum cap))                                                                 \
    X(void, Disable, (GLenum cap))                                                                \
    X(GLboolean, IsEnabled, (GLenum cap))                                                         \
    X(void, GetIntegerv, (GLenum pname, GLint* data))                                             \
    X(GLenum, GetError, ())                                                                       \
    X(void, Viewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                   \
    X(void, Scissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                    \
    X(void, ClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                             \
    X(void, Clear, (GLbitfield mask))                                                             \
    X(void, BlendEquation, (GLenum mode))                                                         \
    X(void, BlendFuncSeparate, (GLenum src_rgb, GLenum dst_rgb, GLenum src_a, GLenum dst_a))      \
    X(void, ActiveTexture, (GLenum texture))                                                      \
    X(void, GenTextures, (GLsizei n, GLuint* textures))                                           \
    X(void, DeleteTextures, (GLsizei n, const GLuint* textures))                                  \
    X(void, BindTexture, (GLenum target, GLuint texture))                                         \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                            \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internal_format, GLsizei w, GLsizei h, \
                         GLint border, GLenum format, GLenum type, const void* pixels))           \
    X(void, GenFramebuffers, (GLsizei n, GLuint* fbs))                                            \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint* fbs))                                   \
    X(void, BindFramebuffer, (GLenum target, GLuint fb))                                          \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget,            \
                                   GLuint texture, GLint level))                                  \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                            \
    X(void, BlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0,   \
                              GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))              \
    X(GLuint, CreateShader, (GLenum type))                                                        \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* strings,            \
                           const GLint* lengths))                                                 \
    X(void, CompileShader, (GLuint shader))                                                       \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                            \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei max_len, GLsizei* len, GLchar* log))        \
    X(void, DeleteShader, (GLuint shader))                                                        \
    X(GLuint, CreateProgram, ())                                                                  \
    X(void, AttachShader, (GLuint program, GLuint shader))                                        \
    X(void, BindAttribLocation, (GLuint program, GLuint index, const GLchar* name))               \
    X(void, LinkProgram, (GLuint program))                                                        \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                          \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei max_len, GLsizei* len, GLchar* log))      \
    X(void, DeleteProgram, (GLuint program))                                                      \
    X(void, UseProgram, (GLuint program))                                                         \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                            \
    X(void, Uniform1i, (GLint loc, GLint v0))                                                     \
    X(void, Uniform1f, (GLint loc, GLfloat v0))                                                   \
    X(void, Uniform2f, (GLint loc, GLfloat v0, GLfloat v1))                                       \
    X(void, Uniform3f, (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2))                           \
    X(void, Uniform4f, (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))               \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                             \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                    \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                           \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))         \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                         \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                \
    X(void, BindVertexArray, (GLuint array))                                                      \
    X(void, EnableVertexAttribArray, (GLuint index))                                              \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized,    \
                                  GLsizei stride, const void* pointer))                           \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))

// missing on GL < 3.3 and GL ES, may be null
#define IMB_FX_GL_FUNCS_OPT(X)                                                                    \
    X(void, BindSampler, (GLuint unit, GLuint sampler))                                           \
    X(void, PolygonMode, (GLenum face, GLenum mode))

struct Functions {
#define IMB_FX_GL_DECL(ret, name, args) ret(IMB_FX_GL_APIENTRY* name) args;
    IMB_FX_GL_FUNCS(IMB_FX_GL_DECL)
    IMB_FX_GL_FUNCS_OPT(IMB_FX_GL_DECL)
#undef IMB_FX_GL_DECL
};

inline Functions& Fn() {
    static Functions f;
    return f;
}

template <class T>
inline bool LoadOne(void* (*get_proc)(const char*), T& out, const char* name) {
    static_assert(sizeof(T) == sizeof(void*), "function pointer size");
    void* p = get_proc(name);
    memcpy(&out, &p, sizeof(p));
    return p != nullptr;
}

inline bool Load(void* (*get_proc)(const char*)) {
    Functions& f = Fn();
    memset(&f, 0, sizeof(f));
    bool ok = true;
#define IMB_FX_GL_LOAD(ret, name, args) ok = LoadOne(get_proc, f.name, "gl" #name) && ok;
    IMB_FX_GL_FUNCS(IMB_FX_GL_LOAD)
#undef IMB_FX_GL_LOAD
#define IMB_FX_GL_LOAD(ret, name, args) LoadOne(get_proc, f.name, "gl" #name);
    IMB_FX_GL_FUNCS_OPT(IMB_FX_GL_LOAD)
#undef IMB_FX_GL_LOAD
    return ok;
}

#undef IMB_FX_GL_FUNCS
#undef IMB_FX_GL_FUNCS_OPT

enum { kLevelCount = 6, kArenaChunkSize = 64 };

struct Target {
    GLuint tex = 0;
    GLuint fbo = 0;
    int w = 0;
    int h = 0;
};

struct CallbackData {
    ImVec2 min;
    ImVec2 max;
    BlurParams params;
};

struct ArenaChunk {
    CallbackData items[kArenaChunkSize];
    ArenaChunk* next = nullptr;
};

struct State {
    bool initialized = false;
    bool has_samplers = false;
    bool has_polygon_mode = false;
    GLuint prog_down = 0;
    GLuint prog_gauss = 0;
    GLuint prog_blur = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint loc_down_texel = -1;
    GLint loc_gauss_dir = -1;
    GLint loc_blur_rect = -1;
    GLint loc_blur_px_rect = -1;
    GLint loc_blur_rounding = -1;
    GLint loc_blur_tint = -1;
    GLint loc_blur_adjust = -1;
    GLint loc_blur_mix = -1;
    Target levels[kLevelCount];   // 0 full-res capture, 1 half, 2+ blurred quarter and smaller
    Target temps[kLevelCount];
    int fb_w = 0;
    int fb_h = 0;
    int prepared_frame = -1000;
    ImVec2 disp_pos;
    ImVec2 disp_size;
    ArenaChunk* arena_first = nullptr;
    ArenaChunk* arena_cur = nullptr;
    int arena_idx = 0;
    char error[512] = {};
};

inline State& S() {
    static State s;
    return s;
}

// rough gaussian sigma per level in pixels, Blur() lerps between two
inline const float* LevelSigma() {
    static const float s[kLevelCount] = {0.0f, 1.0f, 7.5f, 17.0f, 35.0f, 70.0f};
    return s;
}

inline const char* VertexSrc() {
    return "uniform vec4 u_rect;\n"
           "in vec2 a_pos;\n"
           "out vec2 v_uv;\n"
           "void main() {\n"
           "    vec2 p = mix(u_rect.xy, u_rect.zw, a_pos);\n"
           "    v_uv = p * 0.5 + 0.5;\n"
           "    gl_Position = vec4(p, 0.0, 1.0);\n"
           "}\n";
}

inline const char* DownsampleSrc() {
    return "uniform sampler2D u_tex;\n"
           "uniform vec2 u_texel;\n"
           "in vec2 v_uv;\n"
           "out vec4 o_col;\n"
           "void main() {\n"
           "    vec4 d = u_texel.xyxy * vec4(-1.0, -1.0, 1.0, 1.0);\n"
           "    o_col = 0.25 * (texture(u_tex, v_uv + d.xy) + texture(u_tex, v_uv + d.zy) +\n"
           "                    texture(u_tex, v_uv + d.xw) + texture(u_tex, v_uv + d.zw));\n"
           "}\n";
}

inline const char* GaussianSrc() {
    return "uniform sampler2D u_tex;\n"
           "uniform vec2 u_dir;\n"
           "in vec2 v_uv;\n"
           "out vec4 o_col;\n"
           "void main() {\n"
           "    vec4 c = texture(u_tex, v_uv) * 0.2270270270;\n"
           "    vec2 o1 = u_dir * 1.3846153846;\n"
           "    vec2 o2 = u_dir * 3.2307692308;\n"
           "    c += (texture(u_tex, v_uv + o1) + texture(u_tex, v_uv - o1)) * 0.3162162162;\n"
           "    c += (texture(u_tex, v_uv + o2) + texture(u_tex, v_uv - o2)) * 0.0702702703;\n"
           "    o_col = c;\n"
           "}\n";
}

inline const char* BlurSrc() {
    return "uniform sampler2D u_tex0;\n"
           "uniform sampler2D u_tex1;\n"
           "uniform float u_mix;\n"
           "uniform vec4 u_px_rect;\n"
           "uniform float u_rounding;\n"
           "uniform vec4 u_tint;\n"
           "uniform vec3 u_adjust;\n"
           "in vec2 v_uv;\n"
           "out vec4 o_col;\n"
           "float Hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
           "void main() {\n"
           "    vec4 c = mix(texture(u_tex0, v_uv), texture(u_tex1, v_uv), u_mix);\n"
           "    float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
           "    vec3 rgb = mix(vec3(l), c.rgb, u_adjust.x) * u_adjust.y;\n"
           "    rgb += (Hash(gl_FragCoord.xy) - 0.5) * (u_adjust.z / 32.0);\n"
           "    vec2 half_size = (u_px_rect.zw - u_px_rect.xy) * 0.5;\n"
           "    vec2 center = (u_px_rect.xy + u_px_rect.zw) * 0.5;\n"
           "    float r = min(u_rounding, min(half_size.x, half_size.y));\n"
           "    vec2 q = abs(gl_FragCoord.xy - center) - half_size + vec2(r);\n"
           "    float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;\n"
           "    float mask = clamp(0.5 - d, 0.0, 1.0);\n"
           "    o_col = vec4(rgb * u_tint.rgb, u_tint.a * mask);\n"
           "}\n";
}

inline void SetError(const char* what, const char* detail) {
    snprintf(S().error, sizeof(S().error), "%s%s%s", what, detail ? ": " : "",
             detail ? detail : "");
}

inline GLuint CompileShader(GLenum type, const char* version, bool es, const char* body,
                            const char* what) {
    Functions& g = Fn();
    const char* src[4] = {version, "\n", es ? "precision highp float;\nprecision highp int;\n" : "",
                          body};
    GLuint sh = g.CreateShader(type);
    g.ShaderSource(sh, 4, src, nullptr);
    g.CompileShader(sh);
    GLint ok = 0;
    g.GetShaderiv(sh, kCompileStatus, &ok);
    if (!ok) {
        char log[400] = {};
        g.GetShaderInfoLog(sh, (GLsizei)sizeof(log), nullptr, log);
        SetError(what, log);
        g.DeleteShader(sh);
        return 0;
    }
    return sh;
}

inline GLuint LinkProgram(const char* version, bool es, const char* fs_body, const char* what) {
    Functions& g = Fn();
    GLuint vs = CompileShader(kVertexShader, version, es, VertexSrc(), what);
    if (!vs) return 0;
    GLuint fs = CompileShader(kFragmentShader, version, es, fs_body, what);
    if (!fs) {
        g.DeleteShader(vs);
        return 0;
    }
    GLuint prog = g.CreateProgram();
    g.AttachShader(prog, vs);
    g.AttachShader(prog, fs);
    g.BindAttribLocation(prog, 0, "a_pos");
    g.LinkProgram(prog);
    g.DeleteShader(vs);
    g.DeleteShader(fs);
    GLint ok = 0;
    g.GetProgramiv(prog, kLinkStatus, &ok);
    if (!ok) {
        char log[400] = {};
        g.GetProgramInfoLog(prog, (GLsizei)sizeof(log), nullptr, log);
        SetError(what, log);
        g.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

inline bool CreateTarget(Target& t, int w, int h) {
    Functions& g = Fn();
    t.w = w;
    t.h = h;
    g.GenTextures(1, &t.tex);
    g.BindTexture(kTexture2D, t.tex);
    g.TexParameteri(kTexture2D, kTextureMinFilter, (GLint)kLinear);
    g.TexParameteri(kTexture2D, kTextureMagFilter, (GLint)kLinear);
    g.TexParameteri(kTexture2D, kTextureWrapS, (GLint)kClampToEdge);
    g.TexParameteri(kTexture2D, kTextureWrapT, (GLint)kClampToEdge);
    g.TexImage2D(kTexture2D, 0, (GLint)kRgba8, w, h, 0, kRgba, kUnsignedByte, nullptr);
    g.GenFramebuffers(1, &t.fbo);
    g.BindFramebuffer(kFramebuffer, t.fbo);
    g.FramebufferTexture2D(kFramebuffer, kColorAttachment0, kTexture2D, t.tex, 0);
    return g.CheckFramebufferStatus(kFramebuffer) == kFramebufferComplete;
}

inline void DestroyTarget(Target& t) {
    Functions& g = Fn();
    if (t.fbo) g.DeleteFramebuffers(1, &t.fbo);
    if (t.tex) g.DeleteTextures(1, &t.tex);
    t = Target();
}

inline void DestroyTargets() {
    State& s = S();
    for (int k = 0; k < kLevelCount; k++) {
        DestroyTarget(s.levels[k]);
        DestroyTarget(s.temps[k]);
    }
    s.fb_w = 0;
    s.fb_h = 0;
}

inline bool CreateTargets(int w, int h) {
    State& s = S();
    DestroyTargets();
    for (int k = 0; k < kLevelCount; k++) {
        int lw = w >> k, lh = h >> k;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        bool ok = CreateTarget(s.levels[k], lw, lh) && (k < 2 || CreateTarget(s.temps[k], lw, lh));
        if (!ok) {
            DestroyTargets();
            SetError("framebuffer incomplete", nullptr);
            return false;
        }
    }
    s.fb_w = w;
    s.fb_h = h;
    return true;
}

inline void DrawPass(const Target& dst, GLuint src_tex) {
    Functions& g = Fn();
    g.BindFramebuffer(kFramebuffer, dst.fbo);
    g.Viewport(0, 0, dst.w, dst.h);
    g.BindTexture(kTexture2D, src_tex);
    g.DrawArrays(kTriangleStrip, 0, 4);
}

inline void BuildLevels() {
    State& s = S();
    Functions& g = Fn();
    for (int k = 1; k < kLevelCount; k++) {
        const Target& src = s.levels[k - 1];
        const Target& dst = s.levels[k];
        g.UseProgram(s.prog_down);
        g.Uniform2f(s.loc_down_texel, 1.0f / (float)src.w, 1.0f / (float)src.h);
        DrawPass(dst, src.tex);
        if (k < 2) continue;
        g.UseProgram(s.prog_gauss);
        g.Uniform2f(s.loc_gauss_dir, 1.0f / (float)dst.w, 0.0f);
        DrawPass(s.temps[k], dst.tex);
        g.Uniform2f(s.loc_gauss_dir, 0.0f, 1.0f / (float)dst.h);
        DrawPass(dst, s.temps[k].tex);
    }
}

inline CallbackData* ArenaPush() {
    State& s = S();
    if (!s.arena_first) {
        s.arena_first = IM_NEW(ArenaChunk)();
        s.arena_cur = s.arena_first;
        s.arena_idx = 0;
    }
    if (s.arena_idx == kArenaChunkSize) {
        if (!s.arena_cur->next) s.arena_cur->next = IM_NEW(ArenaChunk)();
        s.arena_cur = s.arena_cur->next;
        s.arena_idx = 0;
    }
    return &s.arena_cur->items[s.arena_idx++];
}

inline void ArenaFree() {
    State& s = S();
    for (ArenaChunk* c = s.arena_first; c;) {
        ArenaChunk* next = c->next;
        IM_DELETE(c);
        c = next;
    }
    s.arena_first = s.arena_cur = nullptr;
    s.arena_idx = 0;
}

inline void BlurCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    State& s = S();
    const CallbackData* d = (const CallbackData*)cmd->UserCallbackData;
    if (!s.initialized || s.fb_w <= 0 || d == nullptr) return;
    Functions& g = Fn();
    const float fb_w = (float)s.fb_w, fb_h = (float)s.fb_h;
    const float sx = s.disp_size.x > 0.0f ? fb_w / s.disp_size.x : 1.0f;
    const float sy = s.disp_size.y > 0.0f ? fb_h / s.disp_size.y : 1.0f;
    const float x0 = (d->min.x - s.disp_pos.x) * sx, x1 = (d->max.x - s.disp_pos.x) * sx;
    const float y0 = fb_h - (d->max.y - s.disp_pos.y) * sy;
    const float y1 = fb_h - (d->min.y - s.disp_pos.y) * sy;
    const float cx0 = x0 > 0.0f ? x0 : 0.0f, cy0 = y0 > 0.0f ? y0 : 0.0f;
    const float cx1 = x1 < fb_w ? x1 : fb_w, cy1 = y1 < fb_h ? y1 : fb_h;
    if (cx1 <= cx0 || cy1 <= cy0) return;
    const int sc_x0 = (int)floorf(cx0), sc_y0 = (int)floorf(cy0);
    const int sc_x1 = (int)ceilf(cx1), sc_y1 = (int)ceilf(cy1);

    const BlurParams& p = d->params;
    float radius = p.radius < 0.0f ? 0.0f : (p.radius > 64.0f ? 64.0f : p.radius);
    radius *= sx;
    const float* sig = LevelSigma();
    int lo = 0;
    while (lo + 1 < kLevelCount && radius >= sig[lo + 1]) lo++;
    const int hi = lo + 1 < kLevelCount ? lo + 1 : lo;
    const float mix = hi != lo ? (radius - sig[lo]) / (sig[hi] - sig[lo]) : 0.0f;
    const ImVec4 tint = ImGui::ColorConvertU32ToFloat4(p.tint);

    GLint prev_tex1 = 0, prev_sampler1 = 0;
    g.ActiveTexture(kTexture1);
    g.GetIntegerv(kTextureBinding2D, &prev_tex1);
    if (s.has_samplers) {
        g.GetIntegerv(kSamplerBinding, &prev_sampler1);
        g.BindSampler(1, 0);
    }
    g.BindTexture(kTexture2D, s.levels[hi].tex);
    g.ActiveTexture(kTexture0);
    if (s.has_samplers) g.BindSampler(0, 0);
    g.BindTexture(kTexture2D, s.levels[lo].tex);

    g.UseProgram(s.prog_blur);
    g.BindVertexArray(s.vao);
    g.Enable(kBlend);
    g.BlendEquation(kFuncAdd);
    g.BlendFuncSeparate(kSrcAlpha, kOneMinusSrcAlpha, kOne, kOneMinusSrcAlpha);
    g.Disable(kCullFace);
    g.Disable(kDepthTest);
    g.Enable(kScissorTest);
    g.Scissor(sc_x0, sc_y0, sc_x1 - sc_x0, sc_y1 - sc_y0);
    g.Uniform4f(s.loc_blur_rect, x0 / fb_w * 2.0f - 1.0f, y0 / fb_h * 2.0f - 1.0f,
                x1 / fb_w * 2.0f - 1.0f, y1 / fb_h * 2.0f - 1.0f);
    g.Uniform4f(s.loc_blur_px_rect, x0, y0, x1, y1);
    g.Uniform1f(s.loc_blur_rounding, (p.rounding > 0.0f ? p.rounding : 0.0f) * sx);
    g.Uniform4f(s.loc_blur_tint, tint.x, tint.y, tint.z, tint.w);
    g.Uniform3f(s.loc_blur_adjust, p.saturation, p.brightness, p.noise);
    g.Uniform1f(s.loc_blur_mix, mix);
    g.DrawArrays(kTriangleStrip, 0, 4);

    g.ActiveTexture(kTexture1);
    g.BindTexture(kTexture2D, (GLuint)prev_tex1);
    if (s.has_samplers) g.BindSampler(1, (GLuint)prev_sampler1);
    g.ActiveTexture(kTexture0);
}

inline int ParseGlslVersion(const char* v, bool* es) {
    int ver = 0;
    const char* p = v ? v : "";
    while (*p && (*p < '0' || *p > '9')) p++;
    while (*p >= '0' && *p <= '9') ver = ver * 10 + (*p++ - '0');
    *es = ver == 100 || (v && strstr(v, "es") != nullptr);
    return ver;
}

}  // namespace gl

inline const char* Gl3GetLastError() { return gl::S().error; }

inline bool IsAvailable() { return gl::S().initialized; }

inline void Shutdown() {
    gl::State& s = gl::S();
    gl::Functions& g = gl::Fn();
    if (s.initialized) {
        gl::DestroyTargets();
        if (s.prog_down) g.DeleteProgram(s.prog_down);
        if (s.prog_gauss) g.DeleteProgram(s.prog_gauss);
        if (s.prog_blur) g.DeleteProgram(s.prog_blur);
        if (s.vao) g.DeleteVertexArrays(1, &s.vao);
        if (s.vbo) g.DeleteBuffers(1, &s.vbo);
    }
    gl::ArenaFree();
    s = gl::State();
}

inline bool Init(const Gl3InitDesc& desc) {
    using namespace gl;
    State& s = S();
    if (s.initialized) Shutdown();
    if (!desc.get_proc) {
        SetError("get_proc is null", nullptr);
        return false;
    }
    if (!Load(desc.get_proc)) {
        SetError("missing OpenGL 3 entry points", nullptr);
        return false;
    }
    bool es = false;
    const int ver = ParseGlslVersion(desc.glsl_version, &es);
    if (es ? ver < 300 : ver < 130) {
        SetError("unsupported GLSL version (need 130+ or 300 es)", desc.glsl_version);
        return false;
    }
    Functions& g = Fn();
    s.has_samplers = g.BindSampler != nullptr && (es ? ver >= 300 : ver >= 330);
    s.has_polygon_mode = g.PolygonMode != nullptr && !es;

    GLint last_program = 0, last_vao = 0, last_array_buffer = 0;
    g.GetIntegerv(kCurrentProgram, &last_program);
    g.GetIntegerv(kVertexArrayBinding, &last_vao);
    g.GetIntegerv(kArrayBufferBinding, &last_array_buffer);

    s.prog_down = LinkProgram(desc.glsl_version, es, DownsampleSrc(), "fx downsample");
    s.prog_gauss = LinkProgram(desc.glsl_version, es, GaussianSrc(), "fx gaussian");
    s.prog_blur = LinkProgram(desc.glsl_version, es, BlurSrc(), "fx blur");
    bool ok = s.prog_down && s.prog_gauss && s.prog_blur;
    if (ok) {
        const float quad[8] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        g.GenBuffers(1, &s.vbo);
        g.BindBuffer(kArrayBuffer, s.vbo);
        g.BufferData(kArrayBuffer, (GLsizeiptr)sizeof(quad), quad, kStaticDraw);
        g.GenVertexArrays(1, &s.vao);
        g.BindVertexArray(s.vao);
        g.EnableVertexAttribArray(0);
        g.VertexAttribPointer(0, 2, kFloat, kFalse, 0, nullptr);

        s.loc_down_texel = g.GetUniformLocation(s.prog_down, "u_texel");
        s.loc_gauss_dir = g.GetUniformLocation(s.prog_gauss, "u_dir");
        s.loc_blur_rect = g.GetUniformLocation(s.prog_blur, "u_rect");
        s.loc_blur_px_rect = g.GetUniformLocation(s.prog_blur, "u_px_rect");
        s.loc_blur_rounding = g.GetUniformLocation(s.prog_blur, "u_rounding");
        s.loc_blur_tint = g.GetUniformLocation(s.prog_blur, "u_tint");
        s.loc_blur_adjust = g.GetUniformLocation(s.prog_blur, "u_adjust");
        s.loc_blur_mix = g.GetUniformLocation(s.prog_blur, "u_mix");
        g.UseProgram(s.prog_down);
        g.Uniform4f(g.GetUniformLocation(s.prog_down, "u_rect"), -1.0f, -1.0f, 1.0f, 1.0f);
        g.Uniform1i(g.GetUniformLocation(s.prog_down, "u_tex"), 0);
        g.UseProgram(s.prog_gauss);
        g.Uniform4f(g.GetUniformLocation(s.prog_gauss, "u_rect"), -1.0f, -1.0f, 1.0f, 1.0f);
        g.Uniform1i(g.GetUniformLocation(s.prog_gauss, "u_tex"), 0);
        g.UseProgram(s.prog_blur);
        g.Uniform1i(g.GetUniformLocation(s.prog_blur, "u_tex0"), 0);
        g.Uniform1i(g.GetUniformLocation(s.prog_blur, "u_tex1"), 1);
    }

    g.UseProgram((GLuint)last_program);
    g.BindVertexArray((GLuint)last_vao);
    g.BindBuffer(kArrayBuffer, (GLuint)last_array_buffer);
    if (!ok) {
        char saved[sizeof(s.error)];
        memcpy(saved, s.error, sizeof(saved));
        s.initialized = true;
        Shutdown();
        memcpy(s.error, saved, sizeof(saved));
        return false;
    }
    s.initialized = true;
    s.error[0] = 0;
    return true;
}

inline void PrepareFrame(const FrameDesc& fd) {
    using namespace gl;
    State& s = S();
    if (!s.initialized || ImGui::GetCurrentContext() == nullptr) return;
    Functions& g = Fn();
    s.arena_cur = s.arena_first;
    s.arena_idx = 0;
    s.prepared_frame = ImGui::GetFrameCount();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    s.disp_pos = vp->Pos;
    s.disp_size = vp->Size;
    if (fd.width <= 0 || fd.height <= 0) {
        DestroyTargets();
        return;
    }

    GLint last_read_fbo = 0, last_draw_fbo = 0, last_program = 0, last_active = 0;
    GLint last_tex = 0, last_sampler = 0, last_vao = 0;
    GLint last_viewport[4] = {0, 0, 0, 0}, last_scissor[4] = {0, 0, 0, 0};
    GLint last_polygon[2] = {0, 0};
    g.GetIntegerv(kReadFramebufferBinding, &last_read_fbo);
    g.GetIntegerv(kDrawFramebufferBinding, &last_draw_fbo);
    g.GetIntegerv(kCurrentProgram, &last_program);
    g.GetIntegerv(kVertexArrayBinding, &last_vao);
    g.GetIntegerv(kViewport, last_viewport);
    g.GetIntegerv(kScissorBox, last_scissor);
    g.GetIntegerv(kActiveTexture, &last_active);
    g.ActiveTexture(kTexture0);
    g.GetIntegerv(kTextureBinding2D, &last_tex);
    if (s.has_samplers) g.GetIntegerv(kSamplerBinding, &last_sampler);
    if (s.has_polygon_mode) g.GetIntegerv(kPolygonMode, last_polygon);
    const GLboolean last_blend = g.IsEnabled(kBlend);
    const GLboolean last_scissor_test = g.IsEnabled(kScissorTest);
    const GLboolean last_cull = g.IsEnabled(kCullFace);

    const bool same_size = fd.width == s.fb_w && fd.height == s.fb_h;
    const bool ok = same_size || CreateTargets(fd.width, fd.height);
    if (ok) {
        g.BindFramebuffer(kReadFramebuffer, (GLuint)fd.fbo);
        g.BindFramebuffer(kDrawFramebuffer, s.levels[0].fbo);
        g.BlitFramebuffer(0, 0, fd.width, fd.height, 0, 0, fd.width, fd.height, kColorBufferBit,
                          kNearest);
        g.Disable(kBlend);
        g.Disable(kScissorTest);
        g.Disable(kCullFace);
        if (s.has_polygon_mode) g.PolygonMode(kFrontAndBack, kFill);
        if (s.has_samplers) g.BindSampler(0, 0);
        g.BindVertexArray(s.vao);
        BuildLevels();
    }

    g.UseProgram((GLuint)last_program);
    g.BindVertexArray((GLuint)last_vao);
    g.BindTexture(kTexture2D, (GLuint)last_tex);
    if (s.has_samplers) g.BindSampler(0, (GLuint)last_sampler);
    g.ActiveTexture((GLenum)last_active);
    g.BindFramebuffer(kReadFramebuffer, (GLuint)last_read_fbo);
    g.BindFramebuffer(kDrawFramebuffer, (GLuint)last_draw_fbo);
    g.Viewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
    g.Scissor(last_scissor[0], last_scissor[1], last_scissor[2], last_scissor[3]);
    if (last_blend) g.Enable(kBlend); else g.Disable(kBlend);
    if (last_scissor_test) g.Enable(kScissorTest); else g.Disable(kScissorTest);
    if (last_cull) g.Enable(kCullFace); else g.Disable(kCullFace);
    if (s.has_polygon_mode) g.PolygonMode(kFrontAndBack, (GLenum)last_polygon[0]);
}

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params) {
    gl::State& s = gl::S();
    if (!s.initialized || s.fb_w <= 0 || dl == nullptr) return;
    if (ImGui::GetFrameCount() - s.prepared_frame > 1) return;
    if (max.x <= min.x || max.y <= min.y) return;
    gl::CallbackData* d = gl::ArenaPush();
    d->min = min;
    d->max = max;
    d->params = params;
    dl->AddCallback(&gl::BlurCallback, d);
#if IMGUI_VERSION_NUM >= 19280
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

}  // namespace fx
}  // namespace imb
