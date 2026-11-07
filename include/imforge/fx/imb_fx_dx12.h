#pragma once
// imb::fx Direct3D 12 backend. Include this instead of imb_fx.h. Main viewport only.
//   Init(): needs up to frames_in_flight + 6 consecutive descriptors in the shader-visible
//     CBV_SRV_UAV heap that is bound during ImGui_ImplDX12_RenderDrawData. rtv_format is the
//     one you gave ImGui_ImplDX12_Init.
//   PrepareFrame(fd): records into fd.cmd_list (open, back buffer in RENDER_TARGET). Leaves the
//     SRV heap bound and rebinds fd.back_buffer_rtv if set (no DSV).
//   Blur() callbacks get the command list from ImGui_ImplDX12_RenderState (1.91.4+). On older
//     imgui, render ImGui into the same command list you passed to PrepareFrame.
//   Shutdown() doesn't wait, idle the GPU first.
#include "imb_fx.h"
#ifdef _WIN32
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgiformat.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#define IMB_FX_AVAILABLE 1

namespace imb {
namespace fx {

struct Dx12InitDesc {
    ID3D12Device* device = nullptr;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM;
    int frames_in_flight = 2;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_start = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_start = {};
    UINT srv_descriptor_count = 0;
};

struct FrameDesc {
    ID3D12GraphicsCommandList* cmd_list = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    int width = 0;
    int height = 0;
    int frame_index = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE back_buffer_rtv = {};
};

bool Init(const Dx12InitDesc& desc);
const char* Dx12LastError();

namespace dx12_detail {

enum { kMaxFrames = 8, kMaxLevels = 5, kConstantCount = 32 };

struct Constants {
    float rect[4];
    float display[4];
    float uv0[4];
    float uv1[4];
    float blur[4];
    float fx[4];
    float tint[4];
    float misc[4];
};

struct BlurCmd {
    float x0, y0, x1, y1;
    float disp_x, disp_y, scale_x, scale_y;
    BlurParams params;
};

struct Pending {
    ID3D12Resource* res;
    unsigned retire;
};

struct RenderStateMirror {
    ID3D12Device* device;
    ID3D12GraphicsCommandList* cmd_list;
};

struct State {
    bool initialized = false;
    bool prepared = false;
    ID3D12Device* device = nullptr;
    DXGI_FORMAT rtv_format = DXGI_FORMAT_UNKNOWN;
    int frames = 0;
    int levels = 0;
    ID3D12DescriptorHeap* srv_heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu = {};
    UINT srv_inc = 0;
    ID3D12DescriptorHeap* rtv_heap = nullptr;
    UINT rtv_inc = 0;
    ID3D12RootSignature* root_sig = nullptr;
    ID3D12PipelineState* pso_blur = nullptr;
    ID3D12PipelineState* pso_composite = nullptr;
    ID3D12Resource* capture[kMaxFrames] = {};
    bool capture_ready[kMaxFrames] = {};
    ID3D12Resource* temp = nullptr;
    ID3D12Resource* level[kMaxLevels] = {};
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT bb_format = DXGI_FORMAT_UNKNOWN;
    int cur_capture = 0;
    ID3D12GraphicsCommandList* frame_cmd_list = nullptr;
    unsigned serial = 0;
    ImVector<Pending> pending;
    ImVector<BlurCmd> cmds;
    int cmds_frame = -1;
    char last_error[256] = {};
};

inline State& GetState() {
    static State s;
    return s;
}

inline void SetError(State& st, const char* msg) {
    size_t n = 0;
    for (; msg[n] && n < sizeof(st.last_error) - 1; n++) st.last_error[n] = msg[n];
    st.last_error[n] = 0;
}

template <typename T>
inline void SafeRelease(T*& p) {
    if (p) { p->Release(); p = nullptr; }
}

inline D3D12_CPU_DESCRIPTOR_HANDLE SrvCpu(State& st, int slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = st.srv_cpu;
    h.ptr += (SIZE_T)slot * st.srv_inc;
    return h;
}

inline D3D12_GPU_DESCRIPTOR_HANDLE SrvGpu(State& st, int slot) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = st.srv_gpu;
    h.ptr += (UINT64)slot * st.srv_inc;
    return h;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE Rtv(State& st, int idx) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = st.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)idx * st.rtv_inc;
    return h;
}

inline int LevelSlot(State& st, int level) { return level == 0 ? st.cur_capture : st.frames + level; }

inline void Barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* res, D3D12_RESOURCE_STATES before,
                    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    cl->ResourceBarrier(1, &b);
}

inline DXGI_FORMAT ViewFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return f;
    }
}

inline const char* ShaderSource() {
    return
        "cbuffer C : register(b0) {\n"
        "  float4 rect; float4 display; float4 uv0; float4 uv1;\n"
        "  float4 blur; float4 fx; float4 tint; float4 misc;\n"
        "};\n"
        "Texture2D t0 : register(t0);\n"
        "Texture2D t1 : register(t1);\n"
        "SamplerState s0 : register(s0);\n"
        "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
        "VSOut VSMain(uint id : SV_VertexID) {\n"
        "  float2 p = float2((id & 1) ? rect.z : rect.x, (id & 2) ? rect.w : rect.y);\n"
        "  VSOut o;\n"
        "  o.pos = float4(p.x * display.z * 2.0 - 1.0, 1.0 - p.y * display.w * 2.0, 0.0, 1.0);\n"
        "  o.uv = p * display.zw;\n"
        "  return o;\n"
        "}\n"
        "static const float kW[5] = { 0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541,"
        " 0.0162162162 };\n"
        "float4 PSBlur(VSOut i) : SV_Target {\n"
        "  float2 base = i.uv * uv0.xy;\n"
        "  float3 c = t0.Sample(s0, min(base, uv0.zw)).rgb * kW[0];\n"
        "  [unroll] for (int k = 1; k < 5; ++k) {\n"
        "    float2 o = blur.zw * blur.xy * (float)k;\n"
        "    c += t0.Sample(s0, min(base + o, uv0.zw)).rgb * kW[k];\n"
        "    c += t0.Sample(s0, min(max(base - o, 0.0), uv0.zw)).rgb * kW[k];\n"
        "  }\n"
        "  return float4(c, 1.0);\n"
        "}\n"
        "float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }\n"
        "float4 PSComposite(VSOut i) : SV_Target {\n"
        "  float3 c = lerp(t0.Sample(s0, i.uv).rgb, t1.Sample(s0, i.uv).rgb, fx.y);\n"
        "  float l = dot(c, float3(0.299, 0.587, 0.114));\n"
        "  c = lerp(l.xxx, c, fx.z) * fx.w * tint.rgb;\n"
        "  c += (Hash(i.pos.xy) - 0.5) * misc.x * (1.0 / 32.0);\n"
        "  float2 half_size = (rect.zw - rect.xy) * 0.5;\n"
        "  float2 center = (rect.xy + rect.zw) * 0.5;\n"
        "  float r = min(fx.x, min(half_size.x, half_size.y));\n"
        "  float2 d = abs(i.pos.xy - center) - half_size + r;\n"
        "  float dist = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;\n"
        "  return float4(c, saturate(0.5 - dist) * tint.a);\n"
        "}\n";
}

inline ID3DBlob* Compile(State& st, const char* entry, const char* target) {
    const char* src = ShaderSource();
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), "imb_fx_dx12", nullptr, nullptr, entry, target,
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err);
    if (FAILED(hr)) {
        SetError(st, err ? (const char*)err->GetBufferPointer() : "D3DCompile failed");
        SafeRelease(blob);
    }
    SafeRelease(err);
    return blob;
}

inline bool CreateRootSignature(State& st) {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    for (int i = 0; i < 2; i++) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = (UINT)i;
    }
    D3D12_ROOT_PARAMETER params[3] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = kConstantCount;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (int i = 1; i < 3; i++) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[i].DescriptorTable.NumDescriptorRanges = 1;
        params[i].DescriptorTable.pDescriptorRanges = &ranges[i - 1];
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 3;
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                 D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) {
        SetError(st, err ? (const char*)err->GetBufferPointer() : "D3D12SerializeRootSignature failed");
        SafeRelease(err);
        return false;
    }
    SafeRelease(err);
    hr = st.device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                        IID_PPV_ARGS(&st.root_sig));
    blob->Release();
    if (FAILED(hr)) SetError(st, "CreateRootSignature failed");
    return SUCCEEDED(hr);
}

inline ID3D12PipelineState* CreatePso(State& st, ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT fmt, bool blend) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = st.root_sig;
    pd.VS.pShaderBytecode = vs->GetBufferPointer();
    pd.VS.BytecodeLength = vs->GetBufferSize();
    pd.PS.pShaderBytecode = ps->GetBufferPointer();
    pd.PS.BytecodeLength = ps->GetBufferSize();
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    rt.BlendEnable = blend ? TRUE : FALSE;
    rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
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
    pd.RTVFormats[0] = fmt;
    pd.SampleDesc.Count = 1;
    pd.NodeMask = 1;
    ID3D12PipelineState* pso = nullptr;
    if (FAILED(st.device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        SetError(st, "CreateGraphicsPipelineState failed");
        return nullptr;
    }
    return pso;
}

inline bool CreatePsos(State& st) {
    ID3DBlob* vs = Compile(st, "VSMain", "vs_5_0");
    ID3DBlob* ps_blur = Compile(st, "PSBlur", "ps_5_0");
    ID3DBlob* ps_comp = Compile(st, "PSComposite", "ps_5_0");
    bool ok = vs && ps_blur && ps_comp;
    if (ok) st.pso_blur = CreatePso(st, vs, ps_blur, ViewFormat(st.rtv_format), false);
    if (ok) st.pso_composite = CreatePso(st, vs, ps_comp, st.rtv_format, true);
    SafeRelease(vs);
    SafeRelease(ps_blur);
    SafeRelease(ps_comp);
    return ok && st.pso_blur && st.pso_composite;
}

inline bool CreateTexture(State& st, UINT w, UINT h, DXGI_FORMAT fmt, bool render_target,
                          D3D12_RESOURCE_STATES initial, ID3D12Resource** out) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    hp.CreationNodeMask = 1;
    hp.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = fmt;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = render_target ? D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET : D3D12_RESOURCE_FLAG_NONE;
    HRESULT hr = st.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, initial, nullptr,
                                                    IID_PPV_ARGS(out));
    if (FAILED(hr)) SetError(st, "CreateCommittedResource failed");
    return SUCCEEDED(hr);
}

inline void CreateSrv(State& st, ID3D12Resource* res, DXGI_FORMAT fmt, int slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = fmt;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    st.device->CreateShaderResourceView(res, &sd, SrvCpu(st, slot));
}

inline void Retire(State& st, ID3D12Resource*& res) {
    if (!res) return;
    Pending p;
    p.res = res;
    p.retire = st.serial + (unsigned)st.frames + 1;
    st.pending.push_back(p);
    res = nullptr;
}

inline void FlushPending(State& st, bool all) {
    for (int i = 0; i < st.pending.Size;) {
        if (all || st.pending[i].retire <= st.serial) {
            st.pending[i].res->Release();
            st.pending.erase(st.pending.Data + i);
        } else {
            i++;
        }
    }
}

inline void RetireTargets(State& st) {
    for (int i = 0; i < kMaxFrames; i++) {
        Retire(st, st.capture[i]);
        st.capture_ready[i] = false;
    }
    Retire(st, st.temp);
    for (int i = 0; i < kMaxLevels; i++) Retire(st, st.level[i]);
    st.width = st.height = 0;
    st.prepared = false;
}

inline bool CreateTargets(State& st, const D3D12_RESOURCE_DESC& bb) {
    RetireTargets(st);
    UINT w = (UINT)bb.Width;
    UINT h = bb.Height;
    DXGI_FORMAT cap_view = ViewFormat(bb.Format);
    DXGI_FORMAT lvl_fmt = ViewFormat(st.rtv_format);
    for (int i = 0; i < st.frames; i++) {
        if (!CreateTexture(st, w, h, bb.Format, false, D3D12_RESOURCE_STATE_COPY_DEST, &st.capture[i])) {
            RetireTargets(st);
            return false;
        }
        CreateSrv(st, st.capture[i], cap_view, i);
    }
    UINT tw = w > 1 ? w >> 1 : 1;
    UINT th = h > 1 ? h >> 1 : 1;
    if (!CreateTexture(st, tw, th, lvl_fmt, true, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &st.temp)) {
        RetireTargets(st);
        return false;
    }
    CreateSrv(st, st.temp, lvl_fmt, st.frames);
    st.device->CreateRenderTargetView(st.temp, nullptr, Rtv(st, 0));
    for (int k = 1; k <= st.levels; k++) {
        UINT wk = (w >> k) > 0 ? (w >> k) : 1;
        UINT hk = (h >> k) > 0 ? (h >> k) : 1;
        if (!CreateTexture(st, wk, hk, lvl_fmt, true, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                           &st.level[k - 1])) {
            RetireTargets(st);
            return false;
        }
        CreateSrv(st, st.level[k - 1], lvl_fmt, st.frames + k);
        st.device->CreateRenderTargetView(st.level[k - 1], nullptr, Rtv(st, k));
    }
    st.width = w;
    st.height = h;
    st.bb_format = bb.Format;
    return true;
}

inline void Pass(State& st, ID3D12GraphicsCommandList* cl, ID3D12Resource* target, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                 UINT tw, UINT th, int src_slot, const Constants& c) {
    Barrier(cl, target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)tw, (float)th, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)tw, (LONG)th };
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &sc);
    cl->SetGraphicsRoot32BitConstants(0, kConstantCount, &c, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE src = SrvGpu(st, src_slot);
    cl->SetGraphicsRootDescriptorTable(1, src);
    cl->SetGraphicsRootDescriptorTable(2, src);
    cl->DrawInstanced(4, 1, 0, 0);
    Barrier(cl, target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

inline void ReleaseAll(State& st) {
    RetireTargets(st);
    FlushPending(st, true);
    SafeRelease(st.pso_blur);
    SafeRelease(st.pso_composite);
    SafeRelease(st.root_sig);
    SafeRelease(st.rtv_heap);
    st.device = nullptr;
    st.srv_heap = nullptr;
    st.frame_cmd_list = nullptr;
    st.cmds.clear();
    st.cmds_frame = -1;
    st.serial = 0;
    st.initialized = false;
}

inline void BlurCallback(const ImDrawList* parent_list, const ImDrawCmd* cmd) {
    (void)parent_list;
    State& st = GetState();
    int idx = (int)(intptr_t)cmd->UserCallbackData - 1;
    if (!st.initialized || !st.prepared || idx < 0 || idx >= st.cmds.Size) return;
    ID3D12GraphicsCommandList* cl = nullptr;
#if IMGUI_VERSION_NUM >= 19140
    if (RenderStateMirror* rs = (RenderStateMirror*)ImGui::GetPlatformIO().Renderer_RenderState) cl = rs->cmd_list;
#endif
    if (!cl) cl = st.frame_cmd_list;
    if (!cl) return;

    const BlurCmd& b = st.cmds[idx];
    float sx = b.scale_x > 0.0f ? b.scale_x : 1.0f;
    float sy = b.scale_y > 0.0f ? b.scale_y : 1.0f;
    float x0 = (b.x0 - b.disp_x) * sx, y0 = (b.y0 - b.disp_y) * sy;
    float x1 = (b.x1 - b.disp_x) * sx, y1 = (b.y1 - b.disp_y) * sy;
    float cx0 = (cmd->ClipRect.x - b.disp_x) * sx, cy0 = (cmd->ClipRect.y - b.disp_y) * sy;
    float cx1 = (cmd->ClipRect.z - b.disp_x) * sx, cy1 = (cmd->ClipRect.w - b.disp_y) * sy;
    D3D12_RECT sc;
    sc.left = (LONG)(cx0 > x0 ? cx0 : x0);
    sc.top = (LONG)(cy0 > y0 ? cy0 : y0);
    sc.right = (LONG)ceilf(cx1 < x1 ? cx1 : x1);
    sc.bottom = (LONG)ceilf(cy1 < y1 ? cy1 : y1);
    if (sc.right <= sc.left || sc.bottom <= sc.top) return;

    float radius = b.params.radius < 0.0f ? 0.0f : (b.params.radius > 64.0f ? 64.0f : b.params.radius);
    float f = radius > 0.0f ? log2f(radius * 0.5f) : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > (float)st.levels) f = (float)st.levels;
    int lo = (int)f;
    int hi = lo + 1 > st.levels ? st.levels : lo + 1;
    float t = hi > lo ? f - (float)lo : 0.0f;

    Constants c = {};
    c.rect[0] = x0; c.rect[1] = y0; c.rect[2] = x1; c.rect[3] = y1;
    c.display[0] = (float)st.width;
    c.display[1] = (float)st.height;
    c.display[2] = 1.0f / (float)st.width;
    c.display[3] = 1.0f / (float)st.height;
    c.uv0[0] = c.uv0[1] = c.uv0[2] = c.uv0[3] = 1.0f;
    c.uv1[0] = c.uv1[1] = c.uv1[2] = c.uv1[3] = 1.0f;
    c.fx[0] = b.params.rounding < 0.0f ? 0.0f : b.params.rounding;
    c.fx[1] = t;
    c.fx[2] = b.params.saturation;
    c.fx[3] = b.params.brightness;
    c.tint[0] = (float)((b.params.tint >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    c.tint[1] = (float)((b.params.tint >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    c.tint[2] = (float)((b.params.tint >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    c.tint[3] = (float)((b.params.tint >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    c.misc[0] = b.params.noise;

    cl->SetGraphicsRootSignature(st.root_sig);
    cl->SetPipelineState(st.pso_composite);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cl->SetGraphicsRoot32BitConstants(0, kConstantCount, &c, 0);
    cl->SetGraphicsRootDescriptorTable(1, SrvGpu(st, LevelSlot(st, lo)));
    cl->SetGraphicsRootDescriptorTable(2, SrvGpu(st, LevelSlot(st, hi)));
    cl->RSSetScissorRects(1, &sc);
    cl->DrawInstanced(4, 1, 0, 0);
}

}  // namespace dx12_detail

inline const char* Dx12LastError() { return dx12_detail::GetState().last_error; }

inline bool IsAvailable() { return dx12_detail::GetState().initialized; }

inline void Shutdown() { dx12_detail::ReleaseAll(dx12_detail::GetState()); }

inline bool Init(const Dx12InitDesc& d) {
    using namespace dx12_detail;
    State& st = GetState();
    if (st.initialized) Shutdown();
    if (!d.device || !d.srv_heap || d.frames_in_flight < 1) {
        SetError(st, "invalid Dx12InitDesc");
        return false;
    }
    st.device = d.device;
    st.rtv_format = d.rtv_format;
    st.frames = d.frames_in_flight < kMaxFrames ? d.frames_in_flight : kMaxFrames;
    int avail = (int)d.srv_descriptor_count - st.frames - 1;
    st.levels = avail < kMaxLevels ? avail : kMaxLevels;
    if (st.levels < 1) {
        SetError(st, "srv_descriptor_count must be >= frames_in_flight + 2");
        ReleaseAll(st);
        return false;
    }
    st.srv_heap = d.srv_heap;
    st.srv_cpu = d.srv_cpu_start;
    st.srv_gpu = d.srv_gpu_start;
    st.srv_inc = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    st.rtv_inc = d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1 + kMaxLevels;
    hd.NodeMask = 1;
    if (FAILED(d.device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&st.rtv_heap)))) {
        SetError(st, "CreateDescriptorHeap (RTV) failed");
        ReleaseAll(st);
        return false;
    }
    if (!CreateRootSignature(st) || !CreatePsos(st)) {
        ReleaseAll(st);
        return false;
    }
    st.last_error[0] = 0;
    st.initialized = true;
    return true;
}

inline void PrepareFrame(const FrameDesc& fd) {
    using namespace dx12_detail;
    State& st = GetState();
    if (!st.initialized || !fd.cmd_list || !fd.back_buffer) return;
    D3D12_RESOURCE_DESC bb = fd.back_buffer->GetDesc();
    if (bb.Width == 0 || bb.Height == 0) return;
    st.serial++;
    FlushPending(st, false);
    if ((UINT)bb.Width != st.width || bb.Height != st.height || bb.Format != st.bb_format)
        if (!CreateTargets(st, bb)) return;

    ID3D12GraphicsCommandList* cl = fd.cmd_list;
    int fi = (int)((unsigned)fd.frame_index % (unsigned)st.frames);
    ID3D12Resource* cap = st.capture[fi];
    if (st.capture_ready[fi])
        Barrier(cl, cap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    Barrier(cl, fd.back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyResource(cap, fd.back_buffer);
    Barrier(cl, fd.back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    Barrier(cl, cap, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    st.capture_ready[fi] = true;
    st.cur_capture = fi;

    cl->SetDescriptorHeaps(1, &st.srv_heap);
    cl->SetGraphicsRootSignature(st.root_sig);
    cl->SetPipelineState(st.pso_blur);
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    UINT w = st.width, h = st.height;
    UINT tw = w > 1 ? w >> 1 : 1;
    UINT th = h > 1 ? h >> 1 : 1;
    for (int k = 1; k <= st.levels; k++) {
        UINT wk = (w >> k) > 0 ? (w >> k) : 1;
        UINT hk = (h >> k) > 0 ? (h >> k) : 1;
        Constants c = {};
        c.rect[2] = (float)wk;
        c.rect[3] = (float)hk;
        c.display[0] = (float)wk;
        c.display[1] = (float)hk;
        c.display[2] = 1.0f / (float)wk;
        c.display[3] = 1.0f / (float)hk;
        c.uv0[0] = c.uv0[1] = c.uv0[2] = c.uv0[3] = 1.0f;
        c.blur[0] = 1.0f / (float)wk;
        c.blur[1] = 1.0f / (float)hk;
        c.blur[2] = 1.0f;
        Pass(st, cl, st.temp, Rtv(st, 0), wk, hk, k == 1 ? fi : st.frames + k - 1, c);
        c.uv0[0] = (float)wk / (float)tw;
        c.uv0[1] = (float)hk / (float)th;
        c.uv0[2] = ((float)wk - 0.5f) / (float)tw;
        c.uv0[3] = ((float)hk - 0.5f) / (float)th;
        c.blur[0] = 1.0f / (float)tw;
        c.blur[1] = 1.0f / (float)th;
        c.blur[2] = 0.0f;
        c.blur[3] = 1.0f;
        Pass(st, cl, st.level[k - 1], Rtv(st, k), wk, hk, st.frames, c);
    }
    if (fd.back_buffer_rtv.ptr) {
        cl->OMSetRenderTargets(1, &fd.back_buffer_rtv, FALSE, nullptr);
        D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
        cl->RSSetViewports(1, &vp);
        cl->RSSetScissorRects(1, &sc);
    }
    st.frame_cmd_list = cl;
    st.prepared = true;
}

inline void Blur(ImDrawList* dl, const ImVec2& min, const ImVec2& max, const BlurParams& params) {
    using namespace dx12_detail;
    State& st = GetState();
    if (!dl || !st.initialized || !st.prepared) return;
    if (max.x <= min.x || max.y <= min.y) return;
    int frame = ImGui::GetFrameCount();
    if (frame != st.cmds_frame) {
        st.cmds.resize(0);
        st.cmds_frame = frame;
    }
    BlurCmd cmd;
    cmd.x0 = min.x;
    cmd.y0 = min.y;
    cmd.x1 = max.x;
    cmd.y1 = max.y;
    ImVec2 disp = ImGui::GetMainViewport()->Pos;
    ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    cmd.disp_x = disp.x;
    cmd.disp_y = disp.y;
    cmd.scale_x = scale.x;
    cmd.scale_y = scale.y;
    cmd.params = params;
    st.cmds.push_back(cmd);
    dl->AddCallback(&BlurCallback, (void*)(intptr_t)st.cmds.Size);
#if IMGUI_VERSION_NUM >= 19280
    dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
#else
    dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
#endif
}

}  // namespace fx
}  // namespace imb

#endif  // _WIN32
