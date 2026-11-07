#pragma once
// imb::fx Direct3D 11 backend. Include this instead of imb_fx.h.
//   imb::fx::Dx11InitDesc d; d.device = device; d.context = context; imb::fx::Init(d);
//   each frame, after your scene and before ImGui_ImplDX11_RenderDrawData():
//   imb::fx::FrameDesc fd; fd.back_buffer_rtv = rtv; imb::fx::PrepareFrame(fd);
#include "imb_fx.h"

#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

#define IMB_FX_AVAILABLE 1

namespace imb {
namespace fx {

struct Dx11InitDesc {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
};

// prefer back_buffer_rtv, its format decides sRGB handling. width/height are only hints
struct FrameDesc {
    ID3D11RenderTargetView* back_buffer_rtv = nullptr;
    ID3D11Texture2D* back_buffer = nullptr;
    int width = 0;
    int height = 0;
};

inline bool Init(const Dx11InitDesc& desc);

namespace dx11_detail {

constexpr int kChainLevels = 4;
constexpr int kMaxSlots = 4;
constexpr int kTaps = 7;
constexpr int kPoolChunk = 64;
constexpr float kMaxSigmaPerLevel = 2.4f;
constexpr float kMaxRadius = 64.0f;

inline const char* ShaderSource() {
    return R"hlsl(
cbuffer Constants : register(b0) {
    float4 c0; float4 c1; float4 c2; float4 c3; float4 c4; float4 c5;
};
Texture2D tex0 : register(t0);
SamplerState smp0 : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 PSDown(VSOut i) : SV_Target {
    float2 t = c0.xy;
    float4 s = tex0.Sample(smp0, i.uv + float2(-t.x, -t.y));
    s += tex0.Sample(smp0, i.uv + float2(t.x, -t.y));
    s += tex0.Sample(smp0, i.uv + float2(-t.x, t.y));
    s += tex0.Sample(smp0, i.uv + float2(t.x, t.y));
    return s * 0.25;
}

float4 PSBlur(VSOut i) : SV_Target {
    float w[7] = { c1.x, c1.y, c1.z, c1.w, c2.x, c2.y, c2.z };
    float4 acc = tex0.Sample(smp0, i.uv) * w[0];
    [unroll] for (int k = 1; k < 7; k++) {
        float2 o = c0.xy * (float)k;
        acc += (tex0.Sample(smp0, i.uv + o) + tex0.Sample(smp0, i.uv - o)) * w[k];
    }
    return acc;
}

float4 PSComposite(VSOut i) : SV_Target {
    float2 p = i.pos.xy;
    float4 col = tex0.Sample(smp0, p * c3.zw);
    float2 half_size = (c0.zw - c0.xy) * 0.5;
    float2 center = (c0.xy + c0.zw) * 0.5;
    float r = min(c1.x, min(half_size.x, half_size.y));
    float2 d = abs(p - center) - half_size + r;
    float dist = min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
    float mask = saturate(0.5 - dist);
    float luma = dot(col.rgb, float3(0.299, 0.587, 0.114));
    col.rgb = lerp(luma.xxx, col.rgb, c1.y) * c1.z;
    float n = frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
    col.rgb += (n - 0.5) * c1.w * 0.06;
    col.rgb *= c2.rgb;
    return float4(col.rgb, c2.a * mask);
}
)hlsl";
}

struct Target {
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0;
    int h = 0;
};

struct Params {
    ImVec2 min;
    ImVec2 max;
    float radius;
    float rounding;
    float saturation;
    float brightness;
    float noise;
    ImU32 tint;
    int slot;
};

struct Slot {
    int quant_radius = -1;
    int last_frame = -1000000;
    Target ping;
    Target pong;
};

struct Constants {
    float v[24];
};

struct State {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps_down = nullptr;
    ID3D11PixelShader* ps_blur = nullptr;
    ID3D11PixelShader* ps_composite = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* blend_alpha = nullptr;
    ID3D11RasterizerState* rs_scissor = nullptr;
    ID3D11RasterizerState* rs_plain = nullptr;
    ID3D11DepthStencilState* dss = nullptr;
    ID3D11Texture2D* capture = nullptr;
    ID3D11ShaderResourceView* capture_srv = nullptr;
    DXGI_FORMAT capture_format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT capture_srv_format = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT chain_format = DXGI_FORMAT_UNKNOWN;
    int width = 0;
    int height = 0;
    Target chain[kChainLevels];
    Slot slots[kMaxSlots];
    std::vector<Params*> pool;
    int pool_used = 0;
    int pool_frame = -1;
    ImVec2 display_pos = ImVec2(0.0f, 0.0f);
    bool ready = false;
    bool captured = false;
};

inline State& GetState() {
    static State s;
    return s;
}

template <typename T> inline void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Minf(float a, float b) { return a < b ? a : b; }
inline float Maxf(float a, float b) { return a > b ? a : b; }
inline int MaxI(int a, int b) { return a > b ? a : b; }

inline bool IsSrgbFormat(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
           f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
}

inline DXGI_FORMAT TypelessFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    default: return f;
    }
}

inline DXGI_FORMAT TypedFormat(DXGI_FORMAT f, bool srgb) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return srgb ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        return srgb ? DXGI_FORMAT_B8G8R8X8_UNORM_SRGB : DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return f;
    }
}

inline DXGI_FORMAT ChainFormatFor(DXGI_FORMAT typed) {
    switch (typed) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return IsSrgbFormat(typed) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

struct StateBackup {
    UINT scissor_count = 0;
    UINT viewport_count = 0;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* blend = nullptr;
    float blend_factor[4] = {};
    UINT sample_mask = 0;
    ID3D11DepthStencilState* dss = nullptr;
    UINT stencil_ref = 0;
    ID3D11ShaderResourceView* ps_srv = nullptr;
    ID3D11SamplerState* ps_sampler = nullptr;
    ID3D11Buffer* ps_cb = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11VertexShader* vs = nullptr;
    ID3D11GeometryShader* gs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    UINT ps_inst_count = 0;
    UINT vs_inst_count = 0;
    UINT gs_inst_count = 0;
    ID3D11ClassInstance* ps_inst[256];
    ID3D11ClassInstance* vs_inst[256];
    ID3D11ClassInstance* gs_inst[256];
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11InputLayout* layout = nullptr;

    void Save(ID3D11DeviceContext* ctx) {
        scissor_count = viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        ctx->RSGetScissorRects(&scissor_count, scissors);
        ctx->RSGetViewports(&viewport_count, viewports);
        ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
        ctx->RSGetState(&rs);
        ctx->OMGetBlendState(&blend, blend_factor, &sample_mask);
        ctx->OMGetDepthStencilState(&dss, &stencil_ref);
        ctx->PSGetShaderResources(0, 1, &ps_srv);
        ctx->PSGetSamplers(0, 1, &ps_sampler);
        ctx->PSGetConstantBuffers(0, 1, &ps_cb);
        ps_inst_count = vs_inst_count = gs_inst_count = 256;
        ctx->PSGetShader(&ps, ps_inst, &ps_inst_count);
        ctx->VSGetShader(&vs, vs_inst, &vs_inst_count);
        ctx->GSGetShader(&gs, gs_inst, &gs_inst_count);
        ctx->HSGetShader(&hs, nullptr, nullptr);
        ctx->DSGetShader(&ds, nullptr, nullptr);
        ctx->IAGetPrimitiveTopology(&topology);
        ctx->IAGetInputLayout(&layout);
    }

    void Restore(ID3D11DeviceContext* ctx) {
        ctx->RSSetScissorRects(scissor_count, scissors);
        ctx->RSSetViewports(viewport_count, viewports);
        ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
        for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) SafeRelease(rtvs[i]);
        SafeRelease(dsv);
        ctx->RSSetState(rs);
        SafeRelease(rs);
        ctx->OMSetBlendState(blend, blend_factor, sample_mask);
        SafeRelease(blend);
        ctx->OMSetDepthStencilState(dss, stencil_ref);
        SafeRelease(dss);
        ctx->PSSetShaderResources(0, 1, &ps_srv);
        SafeRelease(ps_srv);
        ctx->PSSetSamplers(0, 1, &ps_sampler);
        SafeRelease(ps_sampler);
        ctx->PSSetConstantBuffers(0, 1, &ps_cb);
        SafeRelease(ps_cb);
        ctx->PSSetShader(ps, ps_inst, ps_inst_count);
        SafeRelease(ps);
        for (UINT i = 0; i < ps_inst_count; i++) SafeRelease(ps_inst[i]);
        ctx->VSSetShader(vs, vs_inst, vs_inst_count);
        SafeRelease(vs);
        for (UINT i = 0; i < vs_inst_count; i++) SafeRelease(vs_inst[i]);
        ctx->GSSetShader(gs, gs_inst, gs_inst_count);
        SafeRelease(gs);
        for (UINT i = 0; i < gs_inst_count; i++) SafeRelease(gs_inst[i]);
        ctx->HSSetShader(hs, nullptr, 0);
        SafeRelease(hs);
        ctx->DSSetShader(ds, nullptr, 0);
        SafeRelease(ds);
        ctx->IASetPrimitiveTopology(topology);
        ctx->IASetInputLayout(layout);
        SafeRelease(layout);
    }
};

inline ID3DBlob* CompileShader(const char* entry, const char* target) {
    const char* src = ShaderSource();
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), "imb_fx_dx11", nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (err) {
        OutputDebugStringA((const char*)err->GetBufferPointer());
        err->Release();
    }
    if (FAILED(hr)) {
        SafeRelease(blob);
        return nullptr;
    }
    return blob;
}

inline bool CreatePS(State& s, const char* entry, ID3D11PixelShader** out) {
    ID3DBlob* b = CompileShader(entry, "ps_4_0");
    if (!b) return false;
    HRESULT hr = s.device->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out);
    b->Release();
    return SUCCEEDED(hr);
}

inline void ReleaseTarget(Target& t) {
    SafeRelease(t.srv);
    SafeRelease(t.rtv);
    SafeRelease(t.tex);
    t.w = t.h = 0;
}

inline bool CreateTarget(State& s, Target& t, int w, int h, DXGI_FORMAT fmt) {
    ReleaseTarget(t);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(s.device->CreateTexture2D(&td, nullptr, &t.tex))) return false;
    if (FAILED(s.device->CreateRenderTargetView(t.tex, nullptr, &t.rtv))) return false;
    if (FAILED(s.device->CreateShaderResourceView(t.tex, nullptr, &t.srv))) return false;
    t.w = w;
    t.h = h;
    return true;
}

inline void ReleaseTargets(State& s) {
    SafeRelease(s.capture_srv);
    SafeRelease(s.capture);
    for (int i = 0; i < kChainLevels; i++) ReleaseTarget(s.chain[i]);
    for (int i = 0; i < kMaxSlots; i++) {
        ReleaseTarget(s.slots[i].ping);
        ReleaseTarget(s.slots[i].pong);
    }
    s.width = s.height = 0;
    s.capture_format = s.capture_srv_format = s.chain_format = DXGI_FORMAT_UNKNOWN;
}

inline bool EnsureTargets(State& s, int w, int h, DXGI_FORMAT bb_format, DXGI_FORMAT typed) {
    if (s.capture && s.width == w && s.height == h && s.capture_format == bb_format &&
        s.capture_srv_format == typed)
        return true;
    ReleaseTargets(s);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = TypelessFormat(bb_format);
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(s.device->CreateTexture2D(&td, nullptr, &s.capture))) return false;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = typed;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    if (FAILED(s.device->CreateShaderResourceView(s.capture, &sd, &s.capture_srv))) {
        ReleaseTargets(s);
        return false;
    }
    s.chain_format = ChainFormatFor(typed);
    for (int i = 0; i < kChainLevels; i++) {
        int cw = MaxI(1, w >> (i + 1));
        int ch = MaxI(1, h >> (i + 1));
        if (!CreateTarget(s, s.chain[i], cw, ch, s.chain_format)) {
            ReleaseTargets(s);
            return false;
        }
    }
    s.width = w;
    s.height = h;
    s.capture_format = bb_format;
    s.capture_srv_format = typed;
    return true;
}

inline void UploadConstants(State& s, const Constants& c) {
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(s.ctx->Map(s.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, &c, sizeof(c));
        s.ctx->Unmap(s.cb, 0);
    }
}

inline void RunPass(State& s, const Target& dst, ID3D11ShaderResourceView* src,
                    const Constants& c, ID3D11PixelShader* ps) {
    UploadConstants(s, c);
    ID3D11ShaderResourceView* null_srv = nullptr;
    s.ctx->PSSetShaderResources(0, 1, &null_srv);
    s.ctx->OMSetRenderTargets(1, &dst.rtv, nullptr);
    D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)dst.w, (float)dst.h, 0.0f, 1.0f};
    s.ctx->RSSetViewports(1, &vp);
    s.ctx->PSSetShader(ps, nullptr, 0);
    s.ctx->PSSetShaderResources(0, 1, &src);
    s.ctx->Draw(3, 0);
}

inline void BindCommon(State& s) {
    s.ctx->IASetInputLayout(nullptr);
    s.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    s.ctx->VSSetShader(s.vs, nullptr, 0);
    s.ctx->GSSetShader(nullptr, nullptr, 0);
    s.ctx->HSSetShader(nullptr, nullptr, 0);
    s.ctx->DSSetShader(nullptr, nullptr, 0);
    s.ctx->PSSetSamplers(0, 1, &s.sampler);
    s.ctx->PSSetConstantBuffers(0, 1, &s.cb);
    s.ctx->OMSetDepthStencilState(s.dss, 0);
}

inline int LevelForRadius(float radius) {
    float sigma_full = radius * 0.5f;
    int level = 1;
    while (level < kChainLevels && sigma_full / (float)(1 << level) > kMaxSigmaPerLevel) level++;
    return level;
}

inline void BlurSlot(State& s, Slot& slot) {
    float radius = (float)slot.quant_radius;
    int level = LevelForRadius(radius);
    const Target& src = s.chain[level - 1];
    float sigma = radius * 0.5f / (float)(1 << level);
    if (slot.ping.w != src.w || slot.ping.h != src.h || !slot.ping.srv || !slot.pong.srv) {
        if (!CreateTarget(s, slot.ping, src.w, src.h, s.chain_format) ||
            !CreateTarget(s, slot.pong, src.w, src.h, s.chain_format)) {
            ReleaseTarget(slot.ping);
            ReleaseTarget(slot.pong);
            return;
        }
    }
    float w[kTaps];
    float sum = 0.0f;
    for (int k = 0; k < kTaps; k++) {
        w[k] = sigma > 0.05f ? expf(-(float)(k * k) / (2.0f * sigma * sigma)) : (k == 0 ? 1.0f : 0.0f);
        sum += k == 0 ? w[k] : 2.0f * w[k];
    }
    Constants c = {};
    for (int k = 0; k < kTaps; k++) c.v[4 + k] = w[k] / sum;
    c.v[0] = 1.0f / (float)src.w;
    c.v[1] = 0.0f;
    RunPass(s, slot.ping, src.srv, c, s.ps_blur);
    c.v[0] = 0.0f;
    c.v[1] = 1.0f / (float)src.h;
    RunPass(s, slot.pong, slot.ping.srv, c, s.ps_blur);
}

inline Params* AllocParams(State& s) {
    int chunk = s.pool_used / kPoolChunk;
    if (chunk >= (int)s.pool.size()) s.pool.push_back(new Params[kPoolChunk]);
    Params* p = &s.pool[(size_t)chunk][s.pool_used % kPoolChunk];
    s.pool_used++;
    return p;
}

inline int AcquireSlot(State& s, int quant_radius, int frame) {
    for (int i = 0; i < kMaxSlots; i++) {
        if (s.slots[i].quant_radius == quant_radius) {
            s.slots[i].last_frame = frame;
            return i;
        }
    }
    int victim = -1;
    for (int i = 0; i < kMaxSlots; i++) {
        if (s.slots[i].last_frame == frame) continue;
        if (victim < 0 || s.slots[i].last_frame < s.slots[victim].last_frame) victim = i;
    }
    if (victim < 0) {
        int best = 0;
        for (int i = 1; i < kMaxSlots; i++) {
            int d = s.slots[i].quant_radius - quant_radius;
            int bd = s.slots[best].quant_radius - quant_radius;
            if ((d < 0 ? -d : d) < (bd < 0 ? -bd : bd)) best = i;
        }
        return best;
    }
    s.slots[victim].quant_radius = quant_radius;
    s.slots[victim].last_frame = frame;
    return victim;
}

inline void BlurCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    State& s = GetState();
    const Params* p = (const Params*)cmd->UserCallbackData;
    if (!s.ready || !s.captured || !p || !s.capture_srv) return;
    ImVec2 disp = s.display_pos;
    ImVec2 scale(1.0f, 1.0f);
    if (ImDrawData* dd = ImGui::GetDrawData()) {
        disp = dd->DisplayPos;
        scale = dd->FramebufferScale;
    }
    float x0 = (p->min.x - disp.x) * scale.x;
    float y0 = (p->min.y - disp.y) * scale.y;
    float x1 = (p->max.x - disp.x) * scale.x;
    float y1 = (p->max.y - disp.y) * scale.y;
    float cx0 = (cmd->ClipRect.x - disp.x) * scale.x;
    float cy0 = (cmd->ClipRect.y - disp.y) * scale.y;
    float cx1 = (cmd->ClipRect.z - disp.x) * scale.x;
    float cy1 = (cmd->ClipRect.w - disp.y) * scale.y;
    int sx0 = (int)floorf(Clampf(Maxf(x0, cx0), 0.0f, (float)s.width));
    int sy0 = (int)floorf(Clampf(Maxf(y0, cy0), 0.0f, (float)s.height));
    int sx1 = (int)ceilf(Clampf(Minf(x1, cx1), 0.0f, (float)s.width));
    int sy1 = (int)ceilf(Clampf(Minf(y1, cy1), 0.0f, (float)s.height));
    if (sx1 <= sx0 || sy1 <= sy0) return;

    ID3D11ShaderResourceView* src = s.capture_srv;
    if (p->slot >= 0 && p->slot < kMaxSlots && s.slots[p->slot].pong.srv)
        src = s.slots[p->slot].pong.srv;

    ImVec4 tint = ImGui::ColorConvertU32ToFloat4(p->tint);
    Constants c = {};
    c.v[0] = x0;
    c.v[1] = y0;
    c.v[2] = x1;
    c.v[3] = y1;
    c.v[4] = p->rounding * scale.x;
    c.v[5] = p->saturation;
    c.v[6] = p->brightness;
    c.v[7] = p->noise;
    c.v[8] = tint.x;
    c.v[9] = tint.y;
    c.v[10] = tint.z;
    c.v[11] = tint.w;
    c.v[12] = (float)s.width;
    c.v[13] = (float)s.height;
    c.v[14] = 1.0f / (float)s.width;
    c.v[15] = 1.0f / (float)s.height;

    StateBackup backup;
    backup.Save(s.ctx);
    UploadConstants(s, c);
    BindCommon(s);
    s.ctx->PSSetShader(s.ps_composite, nullptr, 0);
    s.ctx->PSSetShaderResources(0, 1, &src);
    const float blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    s.ctx->OMSetBlendState(s.blend_alpha, blend_factor, 0xffffffff);
    s.ctx->RSSetState(s.rs_scissor);
    D3D11_VIEWPORT vp = {0.0f, 0.0f, (float)s.width, (float)s.height, 0.0f, 1.0f};
    s.ctx->RSSetViewports(1, &vp);
    const D3D11_RECT scissor = {(LONG)sx0, (LONG)sy0, (LONG)sx1, (LONG)sy1};
    s.ctx->RSSetScissorRects(1, &scissor);
    s.ctx->Draw(3, 0);
    backup.Restore(s.ctx);
}

}  // namespace dx11_detail

inline void Shutdown() {
    using namespace dx11_detail;
    State& s = GetState();
    ReleaseTargets(s);
    for (int i = 0; i < kMaxSlots; i++) {
        s.slots[i].quant_radius = -1;
        s.slots[i].last_frame = -1000000;
    }
    SafeRelease(s.vs);
    SafeRelease(s.ps_down);
    SafeRelease(s.ps_blur);
    SafeRelease(s.ps_composite);
    SafeRelease(s.cb);
    SafeRelease(s.sampler);
    SafeRelease(s.blend_alpha);
    SafeRelease(s.rs_scissor);
    SafeRelease(s.rs_plain);
    SafeRelease(s.dss);
    for (size_t i = 0; i < s.pool.size(); i++) delete[] s.pool[i];
    s.pool.clear();
    s.pool_used = 0;
    s.pool_frame = -1;
    SafeRelease(s.ctx);
    SafeRelease(s.device);
    s.ready = false;
    s.captured = false;
}

inline bool Init(const Dx11InitDesc& desc) {
    using namespace dx11_detail;
    State& s = GetState();
    if (s.device) Shutdown();
    if (!desc.device || !desc.context) return false;
    s.device = desc.device;
    s.device->AddRef();
    s.ctx = desc.context;
    s.ctx->AddRef();

    bool ok = false;
    if (ID3DBlob* b = CompileShader("VSMain", "vs_4_0")) {
        ok = SUCCEEDED(s.device->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(),
                                                    nullptr, &s.vs));
        b->Release();
    }
    ok = ok && CreatePS(s, "PSDown", &s.ps_down) && CreatePS(s, "PSBlur", &s.ps_blur) &&
         CreatePS(s, "PSComposite", &s.ps_composite);
    if (ok) {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Constants);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ok = SUCCEEDED(s.device->CreateBuffer(&bd, nullptr, &s.cb));
    }
    if (ok) {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        ok = SUCCEEDED(s.device->CreateSamplerState(&sd, &s.sampler));
    }
    if (ok) {
        D3D11_BLEND_DESC bd = {};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        ok = SUCCEEDED(s.device->CreateBlendState(&bd, &s.blend_alpha));
    }
    if (ok) {
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.ScissorEnable = TRUE;
        ok = SUCCEEDED(s.device->CreateRasterizerState(&rd, &s.rs_scissor));
        rd.ScissorEnable = FALSE;
        ok = ok && SUCCEEDED(s.device->CreateRasterizerState(&rd, &s.rs_plain));
    }
    if (ok) {
        D3D11_DEPTH_STENCIL_DESC dd = {};
        dd.DepthEnable = FALSE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        ok = SUCCEEDED(s.device->CreateDepthStencilState(&dd, &s.dss));
    }
    if (!ok) {
        Shutdown();
        return false;
    }
    s.ready = true;
    return true;
}

inline bool IsAvailable() { return dx11_detail::GetState().ready; }

inline void PrepareFrame(const FrameDesc& fd) {
    using namespace dx11_detail;
    State& s = GetState();
    s.captured = false;
    if (!s.ready) return;

    ID3D11Texture2D* bb = fd.back_buffer;
    if (bb) {
        bb->AddRef();
    } else if (fd.back_buffer_rtv) {
        ID3D11Resource* res = nullptr;
        fd.back_buffer_rtv->GetResource(&res);
        if (res) {
            res->QueryInterface(IID_PPV_ARGS(&bb));
            res->Release();
        }
    }
    if (!bb) return;

    D3D11_TEXTURE2D_DESC bd;
    bb->GetDesc(&bd);
    bool srgb = IsSrgbFormat(bd.Format);
    if (fd.back_buffer_rtv) {
        D3D11_RENDER_TARGET_VIEW_DESC rd;
        fd.back_buffer_rtv->GetDesc(&rd);
        srgb = srgb || IsSrgbFormat(rd.Format);
    }
    DXGI_FORMAT typed = TypedFormat(bd.Format, srgb);
    if (bd.Width == 0 || bd.Height == 0 ||
        !EnsureTargets(s, (int)bd.Width, (int)bd.Height, bd.Format, typed)) {
        bb->Release();
        return;
    }

    int frame = 0;
    if (ImGui::GetCurrentContext()) {
        s.display_pos = ImGui::GetMainViewport()->Pos;
        frame = ImGui::GetFrameCount();
    }

    StateBackup backup;
    backup.Save(s.ctx);
    if (bd.SampleDesc.Count > 1)
        s.ctx->ResolveSubresource(s.capture, 0, bb, 0, typed);
    else
        s.ctx->CopyResource(s.capture, bb);
    bb->Release();

    BindCommon(s);
    s.ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    s.ctx->RSSetState(s.rs_plain);

    ID3D11ShaderResourceView* src = s.capture_srv;
    int sw = s.width;
    int sh = s.height;
    for (int i = 0; i < kChainLevels; i++) {
        Constants c = {};
        c.v[0] = 1.0f / (float)sw;
        c.v[1] = 1.0f / (float)sh;
        RunPass(s, s.chain[i], src, c, s.ps_down);
        src = s.chain[i].srv;
        sw = s.chain[i].w;
        sh = s.chain[i].h;
    }
    for (int i = 0; i < kMaxSlots; i++) {
        Slot& slot = s.slots[i];
        if (slot.quant_radius < 0 || frame - slot.last_frame > 1) continue;
        BlurSlot(s, slot);
    }
    ID3D11ShaderResourceView* null_srv = nullptr;
    s.ctx->PSSetShaderResources(0, 1, &null_srv);
    backup.Restore(s.ctx);
    s.captured = true;
}

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params) {
    using namespace dx11_detail;
    State& s = GetState();
    if (!s.ready || !dl || !ImGui::GetCurrentContext()) return;
    if (max.x <= min.x || max.y <= min.y) return;
    int frame = ImGui::GetFrameCount();
    if (frame != s.pool_frame) {
        s.pool_frame = frame;
        s.pool_used = 0;
    }
    Params* p = AllocParams(s);
    p->min = min;
    p->max = max;
    p->radius = Clampf(params.radius, 0.0f, kMaxRadius);
    p->rounding = Maxf(params.rounding, 0.0f);
    p->saturation = Maxf(params.saturation, 0.0f);
    p->brightness = Maxf(params.brightness, 0.0f);
    p->noise = Clampf(params.noise, 0.0f, 1.0f);
    p->tint = params.tint;
    p->slot = p->radius < 0.5f ? -1 : AcquireSlot(s, (int)(p->radius + 0.5f), frame);
    dl->AddCallback(&BlurCallback, p);
#if IMGUI_VERSION_NUM >= 19280
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

}  // namespace fx
}  // namespace imb

#endif  // _WIN32
