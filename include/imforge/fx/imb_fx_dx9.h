#pragma once
// imb::fx Direct3D 9 backend. Include this instead of imb_fx.h, in one place only.
//
//   imb::fx::Dx9InitDesc d; d.device = g_pd3dDevice; imb::fx::Init(d);
//   per frame, inside BeginScene()/EndScene(), after your scene:
//     imb::fx::FrameDesc fd; fd.width = backbuffer_w; fd.height = backbuffer_h;
//     imb::fx::PrepareFrame(fd);
//     ImGui::Render(); ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
//   around IDirect3DDevice9::Reset(): imb::fx::OnDeviceLost(); device->Reset(&pp); imb::fx::OnDeviceReset();
//   at exit: imb::fx::Shutdown();
// Needs d3dcompiler_47.dll at runtime (Windows 8.1+ has it). Main viewport only.
#ifdef _WIN32
#include "imb_fx.h"
#include <d3d9.h>
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

#define IMB_FX_AVAILABLE 1

namespace imb {
namespace fx {

struct Dx9InitDesc {
    IDirect3DDevice9* device = nullptr;
};

struct FrameDesc {
    int width = 0;
    int height = 0;
};

bool Init(const Dx9InitDesc& desc);
void OnDeviceLost();
void OnDeviceReset();

namespace dx9_detail {

constexpr int kLevelCount = 5;
constexpr float kLevelOneRadius = 8.0f;
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;

struct Vertex {
    float x, y, z, rhw;
    float u, v;
};

struct CallbackData {
    ImVec2 min;
    ImVec2 max;
    BlurParams params;
};

struct State {
    IDirect3DDevice9* device = nullptr;
    IDirect3DPixelShader9* ps_copy = nullptr;
    IDirect3DPixelShader9* ps_blur = nullptr;
    IDirect3DPixelShader9* ps_composite = nullptr;
    IDirect3DStateBlock9* state_block = nullptr;
    IDirect3DTexture9* capture = nullptr;
    IDirect3DTexture9* level[kLevelCount] = {};
    IDirect3DTexture9* temp[kLevelCount] = {};
    int level_w[kLevelCount] = {};
    int level_h[kLevelCount] = {};
    int fb_w = 0;
    int fb_h = 0;
    bool resources_valid = false;
    bool prepared = false;
    ImVec2 display_pos;
    ImVec2 display_size;
    ImVector<CallbackData*> arena;
    int arena_used = 0;
    int arena_frame = -1;
};

inline State& GetState() {
    static State s;
    return s;
}

template <typename T>
inline void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

inline const char* CopyShaderSource() {
    return R"(
sampler2D tex0 : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR {
    return tex2D(tex0, uv);
}
)";
}

inline const char* BlurShaderSource() {
    return R"(
sampler2D tex0 : register(s0);
float4 c_step : register(c0);
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 o1 = c_step.xy * 1.3846153846;
    float2 o2 = c_step.xy * 3.2307692308;
    float4 c = tex2D(tex0, uv) * 0.2270270270;
    c += (tex2D(tex0, uv + o1) + tex2D(tex0, uv - o1)) * 0.3162162162;
    c += (tex2D(tex0, uv + o2) + tex2D(tex0, uv - o2)) * 0.0702702703;
    return c;
}
)";
}

inline const char* CompositeShaderSource() {
    return R"(
sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);
float4 c_rect : register(c0);
float4 c_misc : register(c1);
float4 c_tint : register(c2);
float4 c_mix  : register(c3);
float4 c_size : register(c4);
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 p = uv * c_size.xy;
    float4 col = lerp(tex2D(tex0, uv), tex2D(tex1, uv), c_mix.x);
    float gray = dot(col.rgb, float3(0.299, 0.587, 0.114));
    col.rgb = lerp(gray.xxx, col.rgb, c_misc.y) * c_misc.z;
    float n = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
    col.rgb += (n - 0.5) * c_misc.w * 0.125;
    col.rgb *= c_tint.rgb;
    float2 q = abs(p - c_rect.xy) - c_rect.zw + c_misc.x;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - c_misc.x;
    float mask = saturate(0.5 - d);
    return float4(col.rgb, c_tint.a * mask);
}
)";
}

inline IDirect3DPixelShader9* CompilePixelShader(IDirect3DDevice9* device, const char* src) {
    static const char* const kTargets[] = { "ps_2_0", "ps_2_b", "ps_3_0" };
    for (const char* target : kTargets) {
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = D3DCompile(src, strlen(src), "imb_fx_dx9", nullptr, nullptr, "main", target,
                                D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        SafeRelease(errors);
        if (FAILED(hr) || !code) {
            SafeRelease(code);
            continue;
        }
        IDirect3DPixelShader9* ps = nullptr;
        hr = device->CreatePixelShader((const DWORD*)code->GetBufferPointer(), &ps);
        code->Release();
        if (SUCCEEDED(hr) && ps) return ps;
    }
    return nullptr;
}

inline void ReleaseResources(State& s) {
    SafeRelease(s.capture);
    for (int i = 0; i < kLevelCount; i++) {
        SafeRelease(s.level[i]);
        SafeRelease(s.temp[i]);
    }
    SafeRelease(s.state_block);
    s.resources_valid = false;
    s.prepared = false;
    s.fb_w = s.fb_h = 0;
}

inline IDirect3DTexture9* CreateRenderTexture(IDirect3DDevice9* device, int w, int h, D3DFORMAT fmt) {
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = device->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, fmt,
                                       D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr) && fmt != D3DFMT_A8R8G8B8) {
        tex = nullptr;
        hr = device->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                   D3DPOOL_DEFAULT, &tex, nullptr);
    }
    return SUCCEEDED(hr) ? tex : nullptr;
}

inline bool CreateResources(State& s, int w, int h, D3DFORMAT bb_format) {
    ReleaseResources(s);
    if (w <= 0 || h <= 0) return false;
    if (FAILED(s.device->CreateStateBlock(D3DSBT_ALL, &s.state_block))) return false;
    s.capture = CreateRenderTexture(s.device, w, h, bb_format);
    if (!s.capture) {
        ReleaseResources(s);
        return false;
    }
    int lw = w, lh = h;
    for (int i = 0; i < kLevelCount; i++) {
        lw = (lw + 1) / 2;
        lh = (lh + 1) / 2;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        s.level_w[i] = lw;
        s.level_h[i] = lh;
        s.level[i] = CreateRenderTexture(s.device, lw, lh, D3DFMT_A8R8G8B8);
        s.temp[i] = (i > 0) ? CreateRenderTexture(s.device, lw, lh, D3DFMT_A8R8G8B8) : nullptr;
        if (!s.level[i] || (i > 0 && !s.temp[i])) {
            ReleaseResources(s);
            return false;
        }
    }
    s.fb_w = w;
    s.fb_h = h;
    s.resources_valid = true;
    return true;
}

inline void SetSampler(IDirect3DDevice9* device, DWORD stage) {
    device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(stage, D3DSAMP_SRGBTEXTURE, 0);
}

inline void SetCommonRenderState(IDirect3DDevice9* device) {
    device->SetVertexShader(nullptr);
    device->SetFVF(kFvf);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPING, TRUE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, 0);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    SetSampler(device, 0);
    SetSampler(device, 1);
}

inline void DrawQuad(IDirect3DDevice9* device, float x0, float y0, float x1, float y1,
                     float u0, float v0, float u1, float v1) {
    const Vertex v[4] = {
        { x0 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, u0, v0 },
        { x1 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, u1, v0 },
        { x0 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, u0, v1 },
        { x1 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, u1, v1 },
    };
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
}

inline bool RenderPass(IDirect3DDevice9* device, IDirect3DTexture9* src, IDirect3DTexture9* dst,
                       int dst_w, int dst_h, IDirect3DPixelShader9* ps, float step_x, float step_y) {
    IDirect3DSurface9* surf = nullptr;
    if (FAILED(dst->GetSurfaceLevel(0, &surf))) return false;
    HRESULT hr = device->SetRenderTarget(0, surf);
    surf->Release();
    if (FAILED(hr)) return false;
    const float consts[4] = { step_x, step_y, 0.0f, 0.0f };
    device->SetPixelShaderConstantF(0, consts, 1);
    device->SetPixelShader(ps);
    device->SetTexture(0, src);
    DrawQuad(device, 0.0f, 0.0f, (float)dst_w, (float)dst_h, 0.0f, 0.0f, 1.0f, 1.0f);
    return true;
}

inline bool CaptureBackBuffer(State& s, const FrameDesc& fd) {
    IDirect3DDevice9* device = s.device;
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return false;
    D3DSURFACE_DESC desc;
    if (FAILED(bb->GetDesc(&desc))) {
        bb->Release();
        return false;
    }
    int w = fd.width > 0 ? fd.width : (int)desc.Width;
    int h = fd.height > 0 ? fd.height : (int)desc.Height;
    if (w > (int)desc.Width) w = (int)desc.Width;
    if (h > (int)desc.Height) h = (int)desc.Height;
    if (!s.resources_valid || s.fb_w != w || s.fb_h != h) {
        if (!CreateResources(s, w, h, desc.Format)) {
            bb->Release();
            return false;
        }
    }
    IDirect3DSurface9* dst = nullptr;
    HRESULT hr = s.capture->GetSurfaceLevel(0, &dst);
    if (SUCCEEDED(hr)) {
        const RECT r = { 0, 0, (LONG)w, (LONG)h };
        const bool full = (w == (int)desc.Width && h == (int)desc.Height);
        hr = device->StretchRect(bb, full ? nullptr : &r, dst, full ? nullptr : &r, D3DTEXF_NONE);
        dst->Release();
    }
    bb->Release();
    return SUCCEEDED(hr);
}

inline bool BuildLevels(State& s) {
    IDirect3DDevice9* device = s.device;
    IDirect3DSurface9* old_rt = nullptr;
    IDirect3DSurface9* old_ds = nullptr;
    device->GetRenderTarget(0, &old_rt);
    device->GetDepthStencilSurface(&old_ds);
    if (SUCCEEDED(s.state_block->Capture())) {
        device->SetDepthStencilSurface(nullptr);
        SetCommonRenderState(device);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        device->SetTexture(1, nullptr);
        bool ok = RenderPass(device, s.capture, s.level[0], s.level_w[0], s.level_h[0], s.ps_copy,
                             0.0f, 0.0f);
        for (int i = 1; ok && i < kLevelCount; i++) {
            const float sx = 1.0f / (float)s.level_w[i];
            const float sy = 1.0f / (float)s.level_h[i];
            ok = RenderPass(device, s.level[i - 1], s.temp[i], s.level_w[i], s.level_h[i], s.ps_blur,
                            sx, 0.0f) &&
                 RenderPass(device, s.temp[i], s.level[i], s.level_w[i], s.level_h[i], s.ps_blur,
                            0.0f, sy);
        }
        device->SetTexture(0, nullptr);
        device->SetRenderTarget(0, old_rt);
        device->SetDepthStencilSurface(old_ds);
        s.state_block->Apply();
        SafeRelease(old_rt);
        SafeRelease(old_ds);
        return ok;
    }
    SafeRelease(old_rt);
    SafeRelease(old_ds);
    return false;
}

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline void BlurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    (void)parent_list;
    State& s = GetState();
    const CallbackData* d = (const CallbackData*)cmd->UserCallbackData;
    if (!d || !s.prepared || !s.resources_valid || !s.device) return;
    IDirect3DDevice9* device = s.device;

    const float scale_x = s.display_size.x > 0.0f ? (float)s.fb_w / s.display_size.x : 1.0f;
    const float scale_y = s.display_size.y > 0.0f ? (float)s.fb_h / s.display_size.y : 1.0f;
    float x0 = (d->min.x - s.display_pos.x) * scale_x;
    float y0 = (d->min.y - s.display_pos.y) * scale_y;
    float x1 = (d->max.x - s.display_pos.x) * scale_x;
    float y1 = (d->max.y - s.display_pos.y) * scale_y;
    const float sx0 = Clamp(x0, 0.0f, (float)s.fb_w), sy0 = Clamp(y0, 0.0f, (float)s.fb_h);
    const float sx1 = Clamp(x1, 0.0f, (float)s.fb_w), sy1 = Clamp(y1, 0.0f, (float)s.fb_h);
    if (sx1 - sx0 < 1.0f || sy1 - sy0 < 1.0f) return;

    const float radius = Clamp(d->params.radius, 0.0f, 64.0f);
    float lvl = radius <= kLevelOneRadius ? radius / kLevelOneRadius
                                          : 1.0f + log2f(radius / kLevelOneRadius);
    lvl = Clamp(lvl, 0.0f, (float)(kLevelCount - 1));
    const int lo = (int)lvl;
    const int hi = lo + 1 < kLevelCount ? lo + 1 : lo;
    const float mix = lvl - (float)lo;
    IDirect3DTexture9* tex_lo = lo == 0 ? s.capture : s.level[lo];
    IDirect3DTexture9* tex_hi = hi == 0 ? s.capture : s.level[hi];

    const float half_w = (x1 - x0) * 0.5f, half_h = (y1 - y0) * 0.5f;
    const float rounding = Clamp(d->params.rounding * (scale_x < scale_y ? scale_x : scale_y), 0.0f,
                                 half_w < half_h ? half_w : half_h);
    const ImVec4 tint = ImGui::ColorConvertU32ToFloat4(d->params.tint);
    const float consts[5 * 4] = {
        x0 + half_w, y0 + half_h, half_w, half_h,
        rounding, d->params.saturation, d->params.brightness, Clamp(d->params.noise, 0.0f, 1.0f),
        tint.x, tint.y, tint.z, tint.w,
        mix, 0.0f, 0.0f, 0.0f,
        (float)s.fb_w, (float)s.fb_h, 1.0f / (float)s.fb_w, 1.0f / (float)s.fb_h,
    };

    if (FAILED(s.state_block->Capture())) return;
    SetCommonRenderState(device);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    const RECT scissor = { (LONG)sx0, (LONG)sy0, (LONG)(sx1 + 0.999f), (LONG)(sy1 + 0.999f) };
    device->SetScissorRect(&scissor);
    device->SetPixelShader(s.ps_composite);
    device->SetPixelShaderConstantF(0, consts, 5);
    device->SetTexture(0, tex_lo);
    device->SetTexture(1, tex_hi);
    DrawQuad(device, sx0, sy0, sx1, sy1, sx0 / (float)s.fb_w, sy0 / (float)s.fb_h,
             sx1 / (float)s.fb_w, sy1 / (float)s.fb_h);
    device->SetTexture(0, nullptr);
    device->SetTexture(1, nullptr);
    s.state_block->Apply();
}

}  // namespace dx9_detail

inline bool Init(const Dx9InitDesc& desc) {
    using namespace dx9_detail;
    Shutdown();
    if (!desc.device) return false;
    State& s = GetState();
    s.device = desc.device;
    s.device->AddRef();
    s.ps_copy = CompilePixelShader(s.device, CopyShaderSource());
    s.ps_blur = CompilePixelShader(s.device, BlurShaderSource());
    s.ps_composite = CompilePixelShader(s.device, CompositeShaderSource());
    if (!s.ps_copy || !s.ps_blur || !s.ps_composite) {
        Shutdown();
        return false;
    }
    return true;
}

inline bool IsAvailable() {
    const dx9_detail::State& s = dx9_detail::GetState();
    return s.device != nullptr && s.ps_composite != nullptr;
}

inline void OnDeviceLost() {
    dx9_detail::ReleaseResources(dx9_detail::GetState());
}

inline void OnDeviceReset() {
    dx9_detail::State& s = dx9_detail::GetState();
    dx9_detail::ReleaseResources(s);
}

inline void Shutdown() {
    using namespace dx9_detail;
    State& s = GetState();
    ReleaseResources(s);
    SafeRelease(s.ps_copy);
    SafeRelease(s.ps_blur);
    SafeRelease(s.ps_composite);
    SafeRelease(s.device);
    for (int i = 0; i < s.arena.Size; i++) IM_DELETE(s.arena[i]);
    s.arena.clear();
    s.arena_used = 0;
    s.arena_frame = -1;
    s.display_pos = s.display_size = ImVec2(0.0f, 0.0f);
}

inline void PrepareFrame(const FrameDesc& fd) {
    using namespace dx9_detail;
    State& s = GetState();
    s.prepared = false;
    if (!IsAvailable()) return;
    if (s.device->TestCooperativeLevel() != D3D_OK) return;
    if (ImGuiViewport* vp = ImGui::GetMainViewport()) {
        s.display_pos = vp->Pos;
        s.display_size = vp->Size;
    }
    if (!CaptureBackBuffer(s, fd)) return;
    if (!BuildLevels(s)) return;
    s.prepared = true;
}

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params) {
    using namespace dx9_detail;
    State& s = GetState();
    if (!dl || !IsAvailable()) return;
    const int frame = ImGui::GetFrameCount();
    if (frame != s.arena_frame) {
        s.arena_frame = frame;
        s.arena_used = 0;
    }
    if (s.arena_used >= s.arena.Size) s.arena.push_back(IM_NEW(CallbackData)());
    CallbackData* d = s.arena[s.arena_used++];
    d->min = min;
    d->max = max;
    d->params = params;
    dl->AddCallback(&BlurCallback, d);
#if IMGUI_VERSION_NUM >= 19280
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

}  // namespace fx
}  // namespace imb

#endif  // _WIN32
