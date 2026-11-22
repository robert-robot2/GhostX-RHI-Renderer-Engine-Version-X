// GhostPost.cpp
#include "../include/GhostPost.h"
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

// ============================================================
//  Tone map. The scene target holds GAMMA-ENCODED floats (what PSMain writes, minus the clamp).
//  decode -> exposure -> + bloom -> curve -> encode. Mode 0 is exactly the old pow(saturate(x), 1/2.2).
// ============================================================
static const char* kTonemapHLSL = R"HLSL(
Texture2D<float4> gHdr   : register(t0);
Texture2D<float4> gBloom : register(t1);
SamplerState sLin : register(s0);
cbuffer TmCB : register(b0)
{
    float4 tm;      // x exposure (linear multiplier), y mode, z white point, w bloom on (0 / 1)
    float4 bl;      // rgb = bloom tint * intensity
};

float4 TmVS(uint vid : SV_VertexID) : SV_POSITION
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    return float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
}

float3 TmReinhard(float3 c, float w) { return c * (1.0f + c / (w * w)) / (1.0f + c); }

float3 TmACES(float3 x)
{
    return saturate((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f));
}

float3 TmNeutral(float3 c)                      // Khronos PBR Neutral: keeps hue and saturation, compresses only the highlights
{
    const float start = 0.76f;
    const float x = min(c.r, min(c.g, c.b));
    const float offset = (x < 0.08f) ? x - 6.25f * x * x : 0.04f;
    c -= offset;
    const float peak = max(c.r, max(c.g, c.b));
    if (peak < start) return c;
    const float d = 1.0f - start;
    const float newPeak = 1.0f - d * d / (peak + d - start);
    c *= newPeak / peak;
    const float g = 1.0f - 1.0f / (0.15f * (peak - newPeak) + 1.0f);
    return lerp(c, float3(newPeak, newPeak, newPeak), g);
}

float4 TmPS(float4 pos : SV_POSITION) : SV_TARGET
{
    float3 enc = gHdr.Load(int3(int2(pos.xy), 0)).rgb;
    float3 lin = pow(max(enc, 0.0f), 2.2f) * tm.x;
    if (tm.w > 0.5f)                                                     // bloom is already linear and already exposed
    {
        uint bw, bh;
        gHdr.GetDimensions(bw, bh);
        lin += gBloom.SampleLevel(sLin, pos.xy / float2((float)bw, (float)bh), 0).rgb * bl.rgb;
    }
    int mode = (int)(tm.y + 0.5f);
    if (mode == 1)      lin = TmReinhard(lin, max(tm.z, 1.0f));
    else if (mode == 2) lin = TmACES(lin);
    else if (mode == 3) lin = TmNeutral(lin);
    return float4(pow(saturate(lin), 1.0f / 2.2f), 1.0f);
}
)HLSL";

// ============================================================
//  Bloom passes. Prefilter reads the gamma-encoded HDR scene, everything after works in linear.
// ============================================================
static const char* kBloomHLSL = R"HLSL(
Texture2D<float4> gSrc : register(t0);
SamplerState sLin : register(s0);               // linear, clamp
cbuffer BloomCB : register(b0)
{
    float4 pa;      // xy = source texel size, z = threshold, w = knee (threshold * soft knee)
    float4 pb;      // x = exposure multiplier
};

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut BloomVS(uint vid : SV_VertexID)
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    o.pos = float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
    o.uv  = float2(c.x, 1.0f - c.y);
    return o;
}

float Luma(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }

float3 Thresh(float3 c)                          // soft knee
{
    float br   = max(c.r, max(c.g, c.b));
    float knee = pa.w;
    float soft = clamp(br - pa.z + knee, 0.0f, 2.0f * knee);
    soft = soft * soft / (4.0f * knee + 1e-5f);
    float contrib = max(soft, br - pa.z) / max(br, 1e-5f);
    return c * contrib;
}

float3 TapPre(float2 uv, float2 o)
{
    float3 enc = gSrc.SampleLevel(sLin, uv + o * pa.xy, 0).rgb;
    return Thresh(pow(max(enc, 0.0f), 2.2f) * pb.x);
}

float3 TapLin(float2 uv, float2 o) { return gSrc.SampleLevel(sLin, uv + o * pa.xy, 0).rgb; }

// 13 taps (offsets in source texels), threshold per tap, Karis average over the five 4-tap groups so one hot pixel can't flicker
float4 BloomPrefilterPS(VSOut i) : SV_TARGET
{
    float2 uv = i.uv;
    float3 A = TapPre(uv, float2(-2,  2)), B = TapPre(uv, float2(0,  2)), C = TapPre(uv, float2(2,  2));
    float3 D = TapPre(uv, float2(-2,  0)), E = TapPre(uv, float2(0,  0)), F = TapPre(uv, float2(2,  0));
    float3 G = TapPre(uv, float2(-2, -2)), H = TapPre(uv, float2(0, -2)), K = TapPre(uv, float2(2, -2));
    float3 J = TapPre(uv, float2(-1,  1)), L = TapPre(uv, float2(1,  1));
    float3 M = TapPre(uv, float2(-1, -1)), N = TapPre(uv, float2(1, -1));

    float3 g0 = (A + B + D + E) * 0.25f, g1 = (B + C + E + F) * 0.25f;
    float3 g2 = (D + E + G + H) * 0.25f, g3 = (E + F + H + K) * 0.25f;
    float3 g4 = (J + L + M + N) * 0.25f;
    float w0 = 0.125f / (1.0f + Luma(g0)), w1 = 0.125f / (1.0f + Luma(g1));
    float w2 = 0.125f / (1.0f + Luma(g2)), w3 = 0.125f / (1.0f + Luma(g3));
    float w4 = 0.5f   / (1.0f + Luma(g4));
    float3 res = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    return float4(res, 1.0f);
}

float4 BloomDownPS(VSOut i) : SV_TARGET
{
    float2 uv = i.uv;
    float3 A = TapLin(uv, float2(-2,  2)), B = TapLin(uv, float2(0,  2)), C = TapLin(uv, float2(2,  2));
    float3 D = TapLin(uv, float2(-2,  0)), E = TapLin(uv, float2(0,  0)), F = TapLin(uv, float2(2,  0));
    float3 G = TapLin(uv, float2(-2, -2)), H = TapLin(uv, float2(0, -2)), K = TapLin(uv, float2(2, -2));
    float3 J = TapLin(uv, float2(-1,  1)), L = TapLin(uv, float2(1,  1));
    float3 M = TapLin(uv, float2(-1, -1)), N = TapLin(uv, float2(1, -1));
    float3 res = E * 0.125f + (A + C + G + K) * 0.03125f + (B + D + F + H) * 0.0625f + (J + L + M + N) * 0.125f;
    return float4(res, 1.0f);
}

float4 BloomUpPS(VSOut i) : SV_TARGET            // 3x3 tent. The scatter blend weight is applied by the blend state
{
    float2 uv = i.uv;
    float3 s = TapLin(uv, float2(-1,  1)) + 2.0f * TapLin(uv, float2(0,  1)) + TapLin(uv, float2(1,  1))
             + 2.0f * TapLin(uv, float2(-1,  0)) + 4.0f * TapLin(uv, float2(0,  0)) + 2.0f * TapLin(uv, float2(1,  0))
             + TapLin(uv, float2(-1, -1)) + 2.0f * TapLin(uv, float2(0, -1)) + TapLin(uv, float2(1, -1));
    return float4(s * (1.0f / 16.0f), 1.0f);
}
)HLSL";

// ============================================================
//  Helpers
// ============================================================
static D3D12_RESOURCE_BARRIER PostTransSub(ID3D12Resource* r, UINT sub, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = sub;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static D3D12_RESOURCE_BARRIER PostTrans(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    return PostTransSub(r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, from, to);
}

static D3D12_CPU_DESCRIPTOR_HANDLE PostHandle(ID3D12DescriptorHeap* heap, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * inc;
    return h;
}

static bool CompilePost(const char* src, const char* entry, const char* target, ID3DBlob** out)
{
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src, strlen(src), "GhostPost", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    return true;
}

static D3D12_STATIC_SAMPLER_DESC LinearClampSampler()
{
    D3D12_STATIC_SAMPLER_DESC s = {};
    s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MaxAnisotropy = 1;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    s.ShaderRegister = 0;
    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return s;
}

// ============================================================
//  Lifecycle
// ============================================================
GhostResult GhostPost_Init(IGhostRHI* rhi, GhostPost& t)
{
    t.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return GHOST_FAIL;

    D3D12_STATIC_SAMPLER_DESC samp = LinearClampSampler();

    // ---- tone map root signature: table of 2 SRVs (hdr, bloom mip 0) + 8 constants + sampler ----
    {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 2;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[0].DescriptorTable.NumDescriptorRanges = 1;
        rp[0].DescriptorTable.pDescriptorRanges = &range;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[1].Constants.ShaderRegister = 0;
        rp[1].Constants.Num32BitValues = 8;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 2;
        rsd.pParameters = rp;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
            FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&t.rootSig))))
            return GHOST_FAIL;
    }
    // ---- bloom root signature: table of 1 SRV + 8 constants + sampler ----
    {
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rp[2] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[0].DescriptorTable.NumDescriptorRanges = 1;
        rp[0].DescriptorTable.pDescriptorRanges = &range;
        rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rp[1].Constants.ShaderRegister = 0;
        rp[1].Constants.Num32BitValues = 8;
        rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 2;
        rsd.pParameters = rp;
        rsd.NumStaticSamplers = 1;
        rsd.pStaticSamplers = &samp;
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
            FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&t.bloomRootSig))))
            return GHOST_FAIL;
    }

    // ---- shaders ----
    ComPtr<ID3DBlob> tmVS, tmPS, bVS, bPre, bDown, bUp;
    if (!CompilePost(kTonemapHLSL, "TmVS", "vs_5_1", &tmVS) || !CompilePost(kTonemapHLSL, "TmPS", "ps_5_1", &tmPS) ||
        !CompilePost(kBloomHLSL, "BloomVS", "vs_5_1", &bVS) || !CompilePost(kBloomHLSL, "BloomPrefilterPS", "ps_5_1", &bPre) ||
        !CompilePost(kBloomHLSL, "BloomDownPS", "ps_5_1", &bDown) || !CompilePost(kBloomHLSL, "BloomUpPS", "ps_5_1", &bUp))
        return GHOST_FAIL;

    // ---- PSOs ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.InputLayout = { nullptr, 0 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tm = d;                         // HDR (+ bloom) -> backbuffer / offscreen
    tm.pRootSignature = t.rootSig.Get();
    tm.VS = { tmVS->GetBufferPointer(), tmVS->GetBufferSize() };
    tm.PS = { tmPS->GetBufferPointer(), tmPS->GetBufferSize() };
    tm.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(dev->CreateGraphicsPipelineState(&tm, IID_PPV_ARGS(&t.tonemapPso)))) return GHOST_FAIL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC bl = d;                         // bloom chain targets are float
    bl.pRootSignature = t.bloomRootSig.Get();
    bl.VS = { bVS->GetBufferPointer(), bVS->GetBufferSize() };
    bl.RTVFormats[0] = GHOST_HDR_FORMAT;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pre = bl;
    pre.PS = { bPre->GetBufferPointer(), bPre->GetBufferSize() };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC dn = bl;
    dn.PS = { bDown->GetBufferPointer(), bDown->GetBufferSize() };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC up = bl;                        // lerp(destination, source, blend factor) = the scatter control
    up.PS = { bUp->GetBufferPointer(), bUp->GetBufferSize() };
    {
        D3D12_RENDER_TARGET_BLEND_DESC& b = up.BlendState.RenderTarget[0];
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_BLEND_FACTOR;      b.DestBlend = D3D12_BLEND_INV_BLEND_FACTOR;   b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ONE;          b.DestBlendAlpha = D3D12_BLEND_ZERO;          b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    if (FAILED(dev->CreateGraphicsPipelineState(&pre, IID_PPV_ARGS(&t.bloomPrefilterPso))) ||
        FAILED(dev->CreateGraphicsPipelineState(&dn, IID_PPV_ARGS(&t.bloomDownPso))) ||
        FAILED(dev->CreateGraphicsPipelineState(&up, IID_PPV_ARGS(&t.bloomUpPso))))
        return GHOST_FAIL;

    // ---- heaps: per surface [hdr, bloom mip 0..5] ----
    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_SURFACES * GHOST_POST_SRV_STRIDE;
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_SURFACES * GHOST_POST_SRV_STRIDE;
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&t.rtvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&t.srvHeap))))
        return GHOST_FAIL;
    t.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    t.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    t.ready = true;
    return GHOST_OK;
}

void GhostPost_Release(GhostPost& t, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    t.hdr[id].Reset();
    t.bloom[id].Reset();
    t.w[id] = t.h[id] = 0;
    t.bloomW[id] = t.bloomH[id] = t.bloomLevels[id] = 0;
    t.bloomValid[id] = false;
}

void GhostPost_Shutdown(GhostPost& t)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostPost_Release(t, i);
    t.tonemapPso.Reset();
    t.bloomPrefilterPso.Reset();
    t.bloomDownPso.Reset();
    t.bloomUpPso.Reset();
    t.rootSig.Reset();
    t.bloomRootSig.Reset();
    t.rtvHeap.Reset();
    t.srvHeap.Reset();
    t.ready = false;
}

GhostResult GhostPost_Ensure(IGhostRHI* rhi, GhostPost& t, int id, unsigned int w, unsigned int h)
{
    if (!t.ready || id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (t.hdr[id] && t.bloom[id] && t.w[id] == w && t.h[id] == h) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    GhostPost_Release(t, id);                                 // the GPU is idle between frames

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = GHOST_HDR_FORMAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    const unsigned int base = (unsigned int)id * GHOST_POST_SRV_STRIDE;

    ComPtr<ID3D12Resource> r;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&r))))
        return GHOST_FAIL;

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = GHOST_HDR_FORMAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev->CreateRenderTargetView(r.Get(), nullptr, PostHandle(t.rtvHeap.Get(), base, t.rtvInc));
    dev->CreateShaderResourceView(r.Get(), &sv, PostHandle(t.srvHeap.Get(), base, t.srvInc));

    // ---- bloom chain: half resolution, smallest mip stays at least 4 pixels ----
    const unsigned int bw = (w / 2) ? (w / 2) : 1u, bh = (h / 2) ? (h / 2) : 1u;
    const unsigned int mn = bw < bh ? bw : bh;
    unsigned int levels = 1;
    while (levels < GHOST_BLOOM_MAX_MIPS && (mn >> levels) >= 4) levels++;

    rd.Width = bw;
    rd.Height = bh;
    rd.MipLevels = (UINT16)levels;
    ComPtr<ID3D12Resource> bc;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&bc))))
        return GHOST_FAIL;

    for (unsigned int m = 0; m < levels; m++)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = GHOST_HDR_FORMAT;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rv.Texture2D.MipSlice = m;
        dev->CreateRenderTargetView(bc.Get(), &rv, PostHandle(t.rtvHeap.Get(), base + 1 + m, t.rtvInc));

        D3D12_SHADER_RESOURCE_VIEW_DESC bv = sv;
        bv.Texture2D.MostDetailedMip = m;
        bv.Texture2D.MipLevels = 1;
        dev->CreateShaderResourceView(bc.Get(), &bv, PostHandle(t.srvHeap.Get(), base + 1 + m, t.srvInc));
    }

    t.hdr[id] = r;
    t.bloom[id] = bc;
    t.w[id] = w;
    t.h[id] = h;
    t.bloomW[id] = bw;
    t.bloomH[id] = bh;
    t.bloomLevels[id] = levels;
    t.bloomValid[id] = false;
    return GHOST_OK;
}

ID3D12Resource* GhostPost_Resource(GhostPost& t, int id)
{
    return (id >= 0 && id < GHOST_MAX_SURFACES) ? t.hdr[id].Get() : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE GhostPost_RTV(GhostPost& t, int id)
{
    return PostHandle(t.rtvHeap.Get(), (unsigned int)id * GHOST_POST_SRV_STRIDE, t.rtvInc);
}

void GhostPost_ToRT(IGhostRHI* rhi, GhostPost& t, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !t.hdr[id]) return;
    D3D12_RESOURCE_BARRIER b = PostTrans(t.hdr[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rhi->GetCmdList()->ResourceBarrier(1, &b);
}

void GhostPost_ToSRV(IGhostRHI* rhi, GhostPost& t, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !t.hdr[id]) return;
    D3D12_RESOURCE_BARRIER b = PostTrans(t.hdr[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    rhi->GetCmdList()->ResourceBarrier(1, &b);
}

void GhostPost_BeginScene(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id, const GhostColor* clear,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !t.hdr[id]) return;
    auto* cmd = rhi->GetCmdList();
    GhostPost_ToRT(rhi, t, id);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GhostPost_RTV(t, id);
    const float c[4] = { clear->r, clear->g, clear->b, clear->a };
    cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    p->rtvOverride = rtv;
    p->rtvOverrideActive = true;
}

void GhostPost_EndScene(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id)
{
    p->rtvOverrideActive = false;
    GhostPost_ToSRV(rhi, t, id);
}

// ============================================================
//  Bloom
// ============================================================
bool GhostPost_Bloom(IGhostRHI* rhi, GhostPost& t, int id, unsigned int w, unsigned int h, const GhostPostSettings& s)
{
    (void)w; (void)h;
    if (id >= 0 && id < GHOST_MAX_SURFACES) t.bloomValid[id] = false;
    if (!t.ready || id < 0 || id >= GHOST_MAX_SURFACES || !s.bloomEnabled || s.bloomIntensity <= 0.0f ||
        !t.hdr[id] || !t.bloom[id]) return false;

    unsigned int levels = t.bloomLevels[id];
    if (levels > (unsigned int)s.bloomMips) levels = (unsigned int)s.bloomMips;
    if (levels < 2) return false;

    auto* cmd = rhi->GetCmdList();
    ID3D12Resource* chain = t.bloom[id].Get();
    const unsigned int base = (unsigned int)id * GHOST_POST_SRV_STRIDE;
    const unsigned int bw = t.bloomW[id], bh = t.bloomH[id];
    auto mipW = [&](unsigned int m) { unsigned int v = bw >> m; return v ? v : 1u; };
    auto mipH = [&](unsigned int m) { unsigned int v = bh >> m; return v ? v : 1u; };

    const float thr = s.bloomThreshold;
    const float knee = thr * s.bloomKnee;
    const float exposure = exp2f(s.exposureEV);
    const float blendF[4] = { s.bloomScatter, s.bloomScatter, s.bloomScatter, s.bloomScatter };

    ID3D12DescriptorHeap* heaps[] = { t.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(t.bloomRootSig.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // one full-screen pass: srcSlot = 0 (the HDR scene) or 1 + mip, dstMip = bloom mip written
    auto pass = [&](ID3D12PipelineState* pso, unsigned int srcSlot, float srcW, float srcH, unsigned int dstMip, bool blend)
        {
            D3D12_RESOURCE_BARRIER b = PostTransSub(chain, dstMip, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ResourceBarrier(1, &b);

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = PostHandle(t.rtvHeap.Get(), base + 1 + dstMip, t.rtvInc);
            cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            const unsigned int dw = mipW(dstMip), dh = mipH(dstMip);
            D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)dw, (float)dh, 0.0f, 1.0f };
            D3D12_RECT sc = { 0, 0, (LONG)dw, (LONG)dh };
            cmd->RSSetViewports(1, &vp);
            cmd->RSSetScissorRects(1, &sc);

            cmd->SetPipelineState(pso);
            D3D12_GPU_DESCRIPTOR_HANDLE g = t.srvHeap->GetGPUDescriptorHandleForHeapStart();
            g.ptr += (UINT64)(base + srcSlot) * t.srvInc;
            cmd->SetGraphicsRootDescriptorTable(0, g);

            const float k[8] = { 1.0f / srcW, 1.0f / srcH, thr, knee, exposure, 0.0f, 0.0f, 0.0f };
            cmd->SetGraphicsRoot32BitConstants(1, 8, k, 0);
            if (blend) cmd->OMSetBlendFactor(blendF);
            cmd->DrawInstanced(3, 1, 0, 0);

            b = PostTransSub(chain, dstMip, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &b);
        };

    pass(t.bloomPrefilterPso.Get(), 0, (float)t.w[id], (float)t.h[id], 0, false);                    // HDR -> mip 0 (threshold)
    for (unsigned int m = 1; m < levels; m++)                                                         // down
        pass(t.bloomDownPso.Get(), 1 + (m - 1), (float)mipW(m - 1), (float)mipH(m - 1), m, false);
    for (int m = (int)levels - 2; m >= 0; m--)                                                        // up, blended into the level above
        pass(t.bloomUpPso.Get(), 1 + (unsigned int)(m + 1), (float)mipW((unsigned int)m + 1), (float)mipH((unsigned int)m + 1), (unsigned int)m, true);

    t.bloomValid[id] = true;
    return true;
}

// ============================================================
//  Tone map (+ bloom composite)
// ============================================================
void GhostPost_Tonemap(IGhostRHI* rhi, GhostPost& t, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest,
    unsigned int w, unsigned int h, const GhostPostSettings& s)
{
    if (!t.ready || id < 0 || id >= GHOST_MAX_SURFACES || !t.hdr[id]) return;
    auto* cmd = rhi->GetCmdList();

    cmd->OMSetRenderTargets(1, &dest, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { t.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(t.tonemapPso.Get());
    cmd->SetGraphicsRootSignature(t.rootSig.Get());

    D3D12_GPU_DESCRIPTOR_HANDLE tbl = t.srvHeap->GetGPUDescriptorHandleForHeapStart();
    tbl.ptr += (UINT64)((unsigned int)id * GHOST_POST_SRV_STRIDE) * t.srvInc;      // [hdr, bloom mip 0]
    cmd->SetGraphicsRootDescriptorTable(0, tbl);

    const float bi = s.bloomIntensity;
    const float k[8] = { exp2f(s.exposureEV), (float)s.toneMode, s.whitePoint, t.bloomValid[id] ? 1.0f : 0.0f,
                         s.bloomTint[0] * bi, s.bloomTint[1] * bi, s.bloomTint[2] * bi, 0.0f };
    cmd->SetGraphicsRoot32BitConstants(1, 8, k, 0);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}