// GhostPost.cpp
#include "../include/GhostPost.h"
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

// The scene target holds GAMMA-ENCODED floats (what PSMain always wrote, minus the clamp).
// So: decode -> exposure -> curve -> encode. Mode 0 is exactly the old pow(saturate(x), 1/2.2).
static const char* kTonemapHLSL = R"HLSL(
Texture2D<float4> gHdr : register(t0);
cbuffer TmCB : register(b0) { float4 tm; };     // x exposure (linear multiplier), y mode, z white point

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
    int mode = (int)(tm.y + 0.5f);
    if (mode == 1)      lin = TmReinhard(lin, max(tm.z, 1.0f));
    else if (mode == 2) lin = TmACES(lin);
    else if (mode == 3) lin = TmNeutral(lin);
    return float4(pow(saturate(lin), 1.0f / 2.2f), 1.0f);
}
)HLSL";

static D3D12_RESOURCE_BARRIER PostTrans(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static D3D12_CPU_DESCRIPTOR_HANDLE PostHandle(ID3D12DescriptorHeap* heap, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * inc;
    return h;
}

GhostResult GhostPost_Init(IGhostRHI* rhi, GhostPost& t)
{
    t.ready = false;
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
    rp[1].Constants.RegisterSpace = 0;
    rp[1].Constants.Num32BitValues = 4;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)) ||
        FAILED(dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&t.rootSig))))
        return GHOST_FAIL;

    ComPtr<ID3DBlob> vs, ps, err;
    const size_t len = strlen(kTonemapHLSL);
    if (FAILED(D3DCompile(kTonemapHLSL, len, "GhostPost", nullptr, nullptr, "TmVS", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }
    err.Reset();
    if (FAILED(D3DCompile(kTonemapHLSL, len, "GhostPost", nullptr, nullptr, "TmPS", "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = t.rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.InputLayout = { nullptr, 0 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;            // backbuffer / offscreen
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;
    if (FAILED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&t.tonemapPso)))) return GHOST_FAIL;

    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_SURFACES;
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_SURFACES;
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
    t.w[id] = t.h[id] = 0;
}

void GhostPost_Shutdown(GhostPost& t)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostPost_Release(t, i);
    t.tonemapPso.Reset();
    t.rootSig.Reset();
    t.rtvHeap.Reset();
    t.srvHeap.Reset();
    t.ready = false;
}

GhostResult GhostPost_Ensure(IGhostRHI* rhi, GhostPost& t, int id, unsigned int w, unsigned int h)
{
    if (!t.ready || id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (t.hdr[id] && t.w[id] == w && t.h[id] == h) return GHOST_OK;

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

    ComPtr<ID3D12Resource> r;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&r))))
        return GHOST_FAIL;

    dev->CreateRenderTargetView(r.Get(), nullptr, PostHandle(t.rtvHeap.Get(), (unsigned int)id, t.rtvInc));

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = GHOST_HDR_FORMAT;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(r.Get(), &sv, PostHandle(t.srvHeap.Get(), (unsigned int)id, t.srvInc));

    t.hdr[id] = r;
    t.w[id] = w;
    t.h[id] = h;
    return GHOST_OK;
}

ID3D12Resource* GhostPost_Resource(GhostPost& t, int id)
{
    return (id >= 0 && id < GHOST_MAX_SURFACES) ? t.hdr[id].Get() : nullptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE GhostPost_RTV(GhostPost& t, int id)
{
    return PostHandle(t.rtvHeap.Get(), (unsigned int)id, t.rtvInc);
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
    tbl.ptr += (UINT64)id * t.srvInc;
    cmd->SetGraphicsRootDescriptorTable(0, tbl);

    const float k[4] = { exp2f(s.exposureEV), (float)s.toneMode, s.whitePoint, 0.0f };
    cmd->SetGraphicsRoot32BitConstants(1, 4, k, 0);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}