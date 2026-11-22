// GhostProbe.cpp
#include "../include/GhostProbe.h"
#include <d3dcompiler.h>
#include <math.h>
#include <string>
#include <string.h>

// ============================================================
//  Shaders. Faces use the D3D cube convention (the same one the hardware samples with).
// ============================================================
static const char* kProbeCommon = R"HLSL(
cbuffer PCB : register(b0)
{
    float4 a;       // x face, y source resolution, z destination resolution, w rotation (radians)
    float4 b;
};
SamplerState sClamp : register(s0);
SamplerState sWrap  : register(s1);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut ProbeVS(uint vid : SV_VertexID)
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    VSOut o;
    o.pos = float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
    o.uv  = float2(c.x, 1.0f - c.y);
    return o;
}

float3 FaceDir(int f, float2 uv)
{
    float s = uv.x * 2.0f - 1.0f, t = uv.y * 2.0f - 1.0f;
    if (f == 0) return float3( 1.0f, -t, -s);
    if (f == 1) return float3(-1.0f, -t,  s);
    if (f == 2) return float3(s,  1.0f,  t);
    if (f == 3) return float3(s, -1.0f, -t);
    if (f == 4) return float3(s, -t,  1.0f);
    return float3(-s, -t, -1.0f);
}
)HLSL";

static const char* kProbePrefilter = R"HLSL(
TextureCube<float4> gCube : register(t0);

static const float2 kOff[13] =
{
    float2(0, 0),
    float2(1.5f, 0), float2(-1.5f, 0), float2(0, 1.5f), float2(0, -1.5f),
    float2(1.5f, 1.5f), float2(-1.5f, 1.5f), float2(1.5f, -1.5f), float2(-1.5f, -1.5f),
    float2(3, 0), float2(-3, 0), float2(0, 3), float2(0, -3)
};
static const float kW[13] =
{
    0.20f,
    0.10f, 0.10f, 0.10f, 0.10f,
    0.05f, 0.05f, 0.05f, 0.05f,
    0.05f, 0.05f, 0.05f, 0.05f
};

float4 PrefilterPS(VSOut i) : SV_TARGET
{
    float3 N  = normalize(FaceDir((int)(a.x + 0.5f), i.uv));
    float3 up = abs(N.y) < 0.99f ? float3(0, 1, 0) : float3(0, 0, 1);
    float3 T  = normalize(cross(up, N));
    float3 B  = cross(N, T);
    float  h  = 2.0f / a.y;                                   // one source texel in tangent-plane units
    float3 sum = float3(0, 0, 0);
    [unroll] for (int k = 0; k < 13; k++)
    {
        float3 d = normalize(N + (T * kOff[k].x + B * kOff[k].y) * h);
        sum += gCube.SampleLevel(sClamp, d, 0).rgb * kW[k];
    }
    return float4(sum, 1.0f);
}
)HLSL";

static const char* kProbeEquirect = R"HLSL(
Texture2D<float4> gEq : register(t0);

float4 EquirectPS(VSOut i) : SV_TARGET
{
    float3 d = normalize(FaceDir((int)(a.x + 0.5f), i.uv));
    float ca = cos(a.w), sa = sin(a.w);
    d = float3(ca * d.x - sa * d.z, d.y, sa * d.x + ca * d.z);     // same rotation convention as the material Reflection map
    float2 uv = float2(atan2(d.x, d.z) / 6.2831853f + 0.5f, acos(clamp(d.y, -1.0f, 1.0f)) / 3.14159265f);
    uint tw, th, lv;
    gEq.GetDimensions(0, tw, th, lv);
    float lod = max(log2((float)tw / (4.0f * a.z)), 0.0f);          // texel size of the texture vs the cube face
    return float4(gEq.SampleLevel(sWrap, uv, lod).rgb, 1.0f);
}
)HLSL";

// ============================================================
//  Helpers
// ============================================================
static D3D12_RESOURCE_BARRIER PTrans(ID3D12Resource* r, UINT sub, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = sub;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static D3D12_CPU_DESCRIPTOR_HANDLE PHandle(ID3D12DescriptorHeap* h, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE c = h->GetCPUDescriptorHandleForHeapStart();
    c.ptr += (SIZE_T)index * inc;
    return c;
}

static unsigned int RtvIndex(int slot, unsigned int mip, int face) { return ((unsigned int)slot * GHOST_PROBE_MAX_MIPS + mip) * 6u + (unsigned int)face; }
static unsigned int ScratchRtvIndex() { return GHOST_MAX_PROBES * GHOST_PROBE_MAX_MIPS * 6u; }
static unsigned int SrvIndex(int slot, unsigned int mip) { return (unsigned int)slot * GHOST_PROBE_MAX_MIPS + mip; }
static unsigned int EqSrvIndex() { return GHOST_MAX_PROBES * GHOST_PROBE_MAX_MIPS; }

static bool CompileProbe(const std::string& src, const char* entry, const char* target, ID3DBlob** out)
{
    ComPtr<ID3DBlob> err;
    if (FAILED(D3DCompile(src.data(), src.size(), "GhostProbe", nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    return true;
}

static void WriteNullProbe(ID3D12Device* dev, GhostPipeline* p, int slot)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC cd = {};
    cd.Format = GHOST_HDR_FORMAT;
    cd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    cd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    cd.TextureCube.MipLevels = 1;
    dev->CreateShaderResourceView(nullptr, &cd, PHandle(p->cbvHeap.Get(), GHOST_PROBE_SRV_BASE + (unsigned int)slot, p->srvIncrement));
}

// ============================================================
//  Lifecycle
// ============================================================
GhostResult GhostProbe_Init(IGhostRHI* rhi, GhostProbeSys& s)
{
    s.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return GHOST_FAIL;

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

    D3D12_STATIC_SAMPLER_DESC ss[2] = {};                      // s0 linear clamp, s1 linear wrap
    for (int i = 0; i < 2; i++)
    {
        ss[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = (i == 0) ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        ss[i].MaxAnisotropy = 1;
        ss[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        ss[i].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        ss[i].MaxLOD = D3D12_FLOAT32_MAX;
        ss[i].ShaderRegister = (UINT)i;
        ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    rsd.NumStaticSamplers = 2;
    rsd.pStaticSamplers = ss;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s.rootSig))))
        return GHOST_FAIL;

    const std::string pre = std::string(kProbeCommon) + kProbePrefilter;
    const std::string eq = std::string(kProbeCommon) + kProbeEquirect;
    ComPtr<ID3DBlob> vs, ps1, ps2;
    if (!CompileProbe(pre, "ProbeVS", "vs_5_1", &vs) || !CompileProbe(pre, "PrefilterPS", "ps_5_1", &ps1) ||
        !CompileProbe(eq, "EquirectPS", "ps_5_1", &ps2))
        return GHOST_FAIL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = s.rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.InputLayout = { nullptr, 0 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = GHOST_HDR_FORMAT;
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC dp = d, de = d;
    dp.PS = { ps1->GetBufferPointer(), ps1->GetBufferSize() };
    de.PS = { ps2->GetBufferPointer(), ps2->GetBufferSize() };
    if (FAILED(dev->CreateGraphicsPipelineState(&dp, IID_PPV_ARGS(&s.prefilterPso))) ||
        FAILED(dev->CreateGraphicsPipelineState(&de, IID_PPV_ARGS(&s.equirectPso))))
        return GHOST_FAIL;

    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_PROBES * GHOST_PROBE_MAX_MIPS * 6u + 1u;
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC dh = {};
    dh.NumDescriptors = 1;
    dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_PROBES * GHOST_PROBE_MAX_MIPS + 1u;
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&s.rtvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&s.dsvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&s.srvHeap))))
        return GHOST_FAIL;
    s.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s.dsvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    s.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    s.ready = true;
    return GHOST_OK;
}

void GhostProbe_Shutdown(GhostProbeSys& s)
{
    for (unsigned int i = 0; i < GHOST_MAX_PROBES; i++) { s.slot[i].cube.Reset(); s.slot[i] = GhostProbeSlot(); }
    s.scratchColor.Reset();
    s.scratchDepth.Reset();
    s.prefilterPso.Reset();
    s.equirectPso.Reset();
    s.rootSig.Reset();
    s.rtvHeap.Reset();
    s.dsvHeap.Reset();
    s.srvHeap.Reset();
    s.ready = false;
}

int GhostProbe_AllocSlot(GhostProbeSys& s)
{
    for (unsigned int i = 0; i < GHOST_MAX_PROBES; i++)
        if (!s.slot[i].inUse) { s.slot[i].inUse = true; return (int)i; }
    return -1;
}

void GhostProbe_ReleaseSlot(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot)
{
    if (slot < 0 || slot >= (int)GHOST_MAX_PROBES) return;
    s.slot[slot].cube.Reset();                                   // the GPU is idle between frames
    s.slot[slot] = GhostProbeSlot();
    if (rhi && p && p->cbvHeap) WriteNullProbe(rhi->GetDevice(), p, slot);
}

GhostResult GhostProbe_EnsureSlot(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, unsigned int res)
{
    if (!s.ready || slot < 0 || slot >= (int)GHOST_MAX_PROBES || res < 16) return GHOST_FAIL;
    GhostProbeSlot& ps = s.slot[slot];
    if (ps.cube && ps.res == res) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    ps.cube.Reset();
    ps.valid = false;
    ps.res = ps.mips = 0;
    WriteNullProbe(dev, p, slot);                                // never leave a stale descriptor behind

    unsigned int mips = 1;
    while ((res >> mips) >= 4 && mips < GHOST_PROBE_MAX_MIPS) mips++;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = res;
    rd.Height = res;
    rd.DepthOrArraySize = 6;
    rd.MipLevels = (UINT16)mips;
    rd.Format = GHOST_HDR_FORMAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> cube;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&cube))))
        return GHOST_FAIL;

    for (unsigned int m = 0; m < mips; m++)
    {
        for (int f = 0; f < 6; f++)
        {
            D3D12_RENDER_TARGET_VIEW_DESC rv = {};
            rv.Format = GHOST_HDR_FORMAT;
            rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rv.Texture2DArray.MipSlice = m;
            rv.Texture2DArray.FirstArraySlice = (UINT)f;
            rv.Texture2DArray.ArraySize = 1;
            dev->CreateRenderTargetView(cube.Get(), &rv, PHandle(s.rtvHeap.Get(), RtvIndex(slot, m, f), s.rtvInc));
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};                 // one mip, for the blur pass that reads it
        sv.Format = GHOST_HDR_FORMAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.TextureCube.MostDetailedMip = m;
        sv.TextureCube.MipLevels = 1;
        dev->CreateShaderResourceView(cube.Get(), &sv, PHandle(s.srvHeap.Get(), SrvIndex(slot, m), s.srvInc));
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC full = {};                   // the whole chain, for the scene shader (t2 + slot)
    full.Format = GHOST_HDR_FORMAT;
    full.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    full.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    full.TextureCube.MostDetailedMip = 0;
    full.TextureCube.MipLevels = mips;
    dev->CreateShaderResourceView(cube.Get(), &full, PHandle(p->cbvHeap.Get(), GHOST_PROBE_SRV_BASE + (unsigned int)slot, p->srvIncrement));

    ps.cube = cube;
    ps.res = res;
    ps.mips = mips;
    return GHOST_OK;
}

GhostResult GhostProbe_EnsureScratch(IGhostRHI* rhi, GhostProbeSys& s, unsigned int res, unsigned int samples)
{
    if (!s.ready) return GHOST_FAIL;
    if (samples < 1) samples = 1;
    if (s.scratchColor && s.scratchDepth && s.scratchRes == res && s.scratchSamples == samples) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    s.scratchColor.Reset();
    s.scratchDepth.Reset();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = res;
    rd.Height = res;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = samples;
    rd.SampleDesc.Quality = 0;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> c, dp;
    rd.Format = GHOST_HDR_FORMAT;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&c)))) return GHOST_FAIL;

    rd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&dp)))) return GHOST_FAIL;

    D3D12_RENDER_TARGET_VIEW_DESC rv = {};
    rv.Format = GHOST_HDR_FORMAT;
    rv.ViewDimension = samples > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
    dev->CreateRenderTargetView(c.Get(), &rv, PHandle(s.rtvHeap.Get(), ScratchRtvIndex(), s.rtvInc));

    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dv.ViewDimension = samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(dp.Get(), &dv, s.dsvHeap->GetCPUDescriptorHandleForHeapStart());

    s.scratchColor = c;
    s.scratchDepth = dp;
    s.scratchRes = res;
    s.scratchSamples = samples;
    return GHOST_OK;
}

// ============================================================
//  Scene capture
// ============================================================
void GhostProbe_BeginFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, const GhostColor* clear, unsigned int res)
{
    if (!s.scratchColor || !s.scratchDepth) return;
    auto* cmd = rhi->GetCmdList();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = PHandle(s.rtvHeap.Get(), ScratchRtvIndex(), s.rtvInc);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = s.dsvHeap->GetCPUDescriptorHandleForHeapStart();

    const float c[4] = { clear->r, clear->g, clear->b, clear->a };
    cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)res, (float)res, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)res, (LONG)res };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    p->rtvOverride = rtv;  p->rtvOverrideActive = true;          // every draw function honors these (same as MSAA / TAA)
    p->dsvOverride = dsv;  p->dsvOverrideActive = true;
}

void GhostProbe_EndFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, int face)
{
    p->rtvOverrideActive = false;
    p->dsvOverrideActive = false;
    if (slot < 0 || slot >= (int)GHOST_MAX_PROBES || !s.scratchColor) return;
    GhostProbeSlot& ps = s.slot[slot];
    if (!ps.cube) return;
    auto* cmd = rhi->GetCmdList();
    const UINT sub = (UINT)face * ps.mips;                       // mip 0 of this face

    if (s.scratchSamples > 1)
    {
        D3D12_RESOURCE_BARRIER b[2];
        b[0] = PTrans(s.scratchColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        b[1] = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmd->ResourceBarrier(2, b);
        cmd->ResolveSubresource(ps.cube.Get(), sub, s.scratchColor.Get(), 0, GHOST_HDR_FORMAT);
        b[0] = PTrans(s.scratchColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        b[1] = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(2, b);
    }
    else
    {
        D3D12_RESOURCE_BARRIER b[2];
        b[0] = PTrans(s.scratchColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        b[1] = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->ResourceBarrier(2, b);

        D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
        dl.pResource = ps.cube.Get();
        dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dl.SubresourceIndex = sub;
        sl.pResource = s.scratchColor.Get();
        sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        sl.SubresourceIndex = 0;
        cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);

        b[0] = PTrans(s.scratchColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        b[1] = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(2, b);
    }
}

// ============================================================
//  Custom-texture capture
// ============================================================
void GhostProbe_EquirectFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, int face, int texId, float yawRad)
{
    if (!s.ready || slot < 0 || slot >= (int)GHOST_MAX_PROBES) return;
    GhostProbeSlot& ps = s.slot[slot];
    ID3D12Resource* tex = (texId > 0 && texId < (int)p->textureCount) ? p->textures[texId].Get() : nullptr;
    if (!ps.cube || !tex) return;
    ID3D12Device* dev = rhi->GetDevice();
    auto* cmd = rhi->GetCmdList();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};                     // the raw (UNORM) view: the bytes are gamma-encoded already, like the cube
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MostDetailedMip = 0;
    sv.Texture2D.MipLevels = (UINT)-1;
    dev->CreateShaderResourceView(tex, &sv, PHandle(s.srvHeap.Get(), EqSrvIndex(), s.srvInc));

    const UINT sub = (UINT)face * ps.mips;
    D3D12_RESOURCE_BARRIER b = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = PHandle(s.rtvHeap.Get(), RtvIndex(slot, 0, face), s.rtvInc);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)ps.res, (float)ps.res, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)ps.res, (LONG)ps.res };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { s.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(s.rootSig.Get());
    cmd->SetPipelineState(s.equirectPso.Get());
    D3D12_GPU_DESCRIPTOR_HANDLE g = s.srvHeap->GetGPUDescriptorHandleForHeapStart();
    g.ptr += (UINT64)EqSrvIndex() * s.srvInc;
    cmd->SetGraphicsRootDescriptorTable(0, g);
    const float k[8] = { (float)face, 0.0f, (float)ps.res, yawRad, 0.0f, 0.0f, 0.0f, 0.0f };
    cmd->SetGraphicsRoot32BitConstants(1, 8, k, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    b = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &b);
}

// ============================================================
//  Roughness mips: each level is a wide blur of the one above it
// ============================================================
void GhostProbe_Prefilter(IGhostRHI* rhi, GhostProbeSys& s, int slot)
{
    if (!s.ready || slot < 0 || slot >= (int)GHOST_MAX_PROBES) return;
    GhostProbeSlot& ps = s.slot[slot];
    if (!ps.cube || ps.mips < 2) return;
    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { s.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(s.rootSig.Get());
    cmd->SetPipelineState(s.prefilterPso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (unsigned int m = 1; m < ps.mips; m++)
    {
        const unsigned int dst = ps.res >> m, src = ps.res >> (m - 1);
        D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)dst, (float)dst, 0.0f, 1.0f };
        D3D12_RECT sc = { 0, 0, (LONG)dst, (LONG)dst };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);

        D3D12_GPU_DESCRIPTOR_HANDLE g = s.srvHeap->GetGPUDescriptorHandleForHeapStart();
        g.ptr += (UINT64)SrvIndex(slot, m - 1) * s.srvInc;
        cmd->SetGraphicsRootDescriptorTable(0, g);

        for (int f = 0; f < 6; f++)
        {
            const UINT sub = m + (UINT)f * ps.mips;
            D3D12_RESOURCE_BARRIER b = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
            cmd->ResourceBarrier(1, &b);

            D3D12_CPU_DESCRIPTOR_HANDLE rtv = PHandle(s.rtvHeap.Get(), RtvIndex(slot, m, f), s.rtvInc);
            cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            const float k[8] = { (float)f, (float)src, (float)dst, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
            cmd->SetGraphicsRoot32BitConstants(1, 8, k, 0);
            cmd->DrawInstanced(3, 1, 0, 0);

            b = PTrans(ps.cube.Get(), sub, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &b);
        }
    }
}