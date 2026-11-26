// GhostFXAA.cpp
#include "../include/GhostFXAA.h"
#include <d3dcompiler.h>
#include <string.h>

// ============================================================
//  Shader (embedded). Luma comes from the tone mapped, gamma-encoded image.
// ============================================================
static const char* kFxaaHLSL = R"HLSL(
Texture2D<float4> gSrc : register(t0);
SamplerState sLin : register(s0);                // linear, clamp

cbuffer FxaaCB : register(b0)
{
    float4 a;       // x 1/width, y 1/height, z subpixel quality (0..1), w relative edge threshold
    float4 b;       // x minimum edge threshold (dark areas), y max search steps
};

float4 FxaaVS(uint vid : SV_VertexID) : SV_POSITION
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    return float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
}

float Luma(float3 c) { return dot(c, float3(0.299f, 0.587f, 0.114f)); }
float3 Fetch(float2 uv) { return gSrc.SampleLevel(sLin, uv, 0).rgb; }
float LumaAt(float2 uv) { return Luma(Fetch(uv)); }

float StepMul(int i)                             // the search accelerates: fine near the pixel, coarse far away
{
    return i < 4 ? 1.0f : (i == 4 ? 1.5f : (i < 9 ? 2.0f : (i == 9 ? 4.0f : 8.0f)));
}

float4 FxaaPS(float4 sp : SV_POSITION) : SV_TARGET
{
    const float2 rcp = a.xy;
    const float2 uv = sp.xy * rcp;

    const float3 rgbM = Fetch(uv);
    const float lumaM = Luma(rgbM);
    float lumaN = LumaAt(uv + float2( 0, -1) * rcp);
    float lumaS = LumaAt(uv + float2( 0,  1) * rcp);
    const float lumaW = LumaAt(uv + float2(-1,  0) * rcp);
    const float lumaE = LumaAt(uv + float2( 1,  0) * rcp);

    const float rangeMax = max(max(lumaN, lumaS), max(lumaE, max(lumaW, lumaM)));
    const float rangeMin = min(min(lumaN, lumaS), min(lumaE, min(lumaW, lumaM)));
    const float range = rangeMax - rangeMin;
    if (range < max(b.x, rangeMax * a.w)) return float4(rgbM, 1.0f);           // not an edge

    const float lumaNW = LumaAt(uv + float2(-1, -1) * rcp);
    const float lumaNE = LumaAt(uv + float2( 1, -1) * rcp);
    const float lumaSW = LumaAt(uv + float2(-1,  1) * rcp);
    const float lumaSE = LumaAt(uv + float2( 1,  1) * rcp);

    const float lumaNS = lumaN + lumaS, lumaWE = lumaW + lumaE;
    const float lumaNESE = lumaNE + lumaSE, lumaNWNE = lumaNW + lumaNE;
    const float lumaNWSW = lumaNW + lumaSW, lumaSWSE = lumaSW + lumaSE;

    // ---- horizontal or vertical edge? (second derivative along each axis, over three rows / columns) ----
    const float edgeHorz = abs(-2.0f * lumaW + lumaNWSW) + abs(-2.0f * lumaM + lumaNS) * 2.0f + abs(-2.0f * lumaE + lumaNESE);
    const float edgeVert = abs(-2.0f * lumaS + lumaSWSE) + abs(-2.0f * lumaM + lumaWE) * 2.0f + abs(-2.0f * lumaN + lumaNWNE);
    const bool  horzSpan = edgeHorz >= edgeVert;

    // ---- subpixel aliasing: how much this pixel differs from its 3x3 average ----
    const float subpixA = (lumaNS + lumaWE) * 2.0f + (lumaNWSW + lumaNESE);
    const float subpixB = subpixA * (1.0f / 12.0f) - lumaM;
    const float subpixC = saturate(abs(subpixB) / max(range, 1e-5f));
    const float subpixF = (3.0f - 2.0f * subpixC) * subpixC * subpixC;          // smoothstep
    const float subpixH = subpixF * subpixF * a.z;

    // ---- which side of the pixel does the edge lie on? ----
    float lengthSign = horzSpan ? rcp.y : rcp.x;
    if (!horzSpan) { lumaN = lumaW; lumaS = lumaE; }
    const float gradientN = lumaN - lumaM, gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    const float lumaSS = lumaS + lumaM;
    const bool  pairN = abs(gradientN) >= abs(gradientS);
    const float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    else lumaNN = lumaSS;

    // ---- search along the edge in both directions, half a pixel toward the edge ----
    float2 posB = uv;
    const float2 offNP = horzSpan ? float2(rcp.x, 0.0f) : float2(0.0f, rcp.y);
    if (horzSpan) posB.y += lengthSign * 0.5f; else posB.x += lengthSign * 0.5f;

    float2 posN = posB - offNP, posP = posB + offNP;
    const float gradientScaled = gradient * 0.25f;
    const float lumaMM = lumaM - lumaNN * 0.5f;
    const bool  lumaMLTZero = lumaMM < 0.0f;

    float lumaEndN = LumaAt(posN) - lumaNN * 0.5f;
    float lumaEndP = LumaAt(posP) - lumaNN * 0.5f;
    bool  doneN = abs(lumaEndN) >= gradientScaled;
    bool  doneP = abs(lumaEndP) >= gradientScaled;
    const int steps = (int)(b.y + 0.5f);
    [loop] for (int i = 0; i < steps; i++)
    {
        if (doneN && doneP) break;
        const float m = StepMul(i);
        if (!doneN) { posN -= offNP * m; lumaEndN = LumaAt(posN) - lumaNN * 0.5f; doneN = abs(lumaEndN) >= gradientScaled; }
        if (!doneP) { posP += offNP * m; lumaEndP = LumaAt(posP) - lumaNN * 0.5f; doneP = abs(lumaEndP) >= gradientScaled; }
    }

    // ---- how far along the edge is this pixel? the closer to an end, the more it is pulled across ----
    const float dstN = horzSpan ? uv.x - posN.x : uv.y - posN.y;
    const float dstP = horzSpan ? posP.x - uv.x : posP.y - uv.y;
    const bool  directionN = dstN < dstP;
    const float lumaEndG = directionN ? lumaEndN : lumaEndP;
    const float spanLength = max(dstP + dstN, 1e-6f);
    const float dstM = directionN ? dstN : dstP;
    const bool  goodSpan = (lumaEndG < 0.0f) != lumaMLTZero;                    // the edge end must lean the right way
    const float pixelOffset = dstM * (-1.0f / spanLength) + 0.5f;
    const float pixelOffsetGood = goodSpan ? pixelOffset : 0.0f;
    const float pixelOffsetFinal = max(pixelOffsetGood, subpixH);

    float2 fuv = uv;
    if (horzSpan) fuv.y += pixelOffsetFinal * lengthSign;
    else          fuv.x += pixelOffsetFinal * lengthSign;
    return float4(Fetch(fuv), 1.0f);
}
)HLSL";

static D3D12_CPU_DESCRIPTOR_HANDLE FHandle(ID3D12DescriptorHeap* heap, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * inc;
    return h;
}

static D3D12_RESOURCE_BARRIER FTrans(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

// ============================================================
//  Lifecycle
// ============================================================
GhostResult GhostFXAA_Init(IGhostRHI* rhi, GhostFXAA& f)
{
    f.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return GHOST_FAIL;

    // ---- root signature: 1 SRV table, 8 root constants, one linear clamp sampler ----
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

    D3D12_STATIC_SAMPLER_DESC ss = {};
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss.MaxAnisotropy = 1;
    ss.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    ss.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    ss.MaxLOD = D3D12_FLOAT32_MAX;
    ss.ShaderRegister = 0;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &ss;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&f.rootSig))))
        return GHOST_FAIL;

    // ---- shaders ----
    ComPtr<ID3DBlob> vs, ps;
    const size_t len = strlen(kFxaaHLSL);
    if (FAILED(D3DCompile(kFxaaHLSL, len, "GhostFXAA", nullptr, nullptr, "FxaaVS", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }
    err.Reset();
    if (FAILED(D3DCompile(kFxaaHLSL, len, "GhostFXAA", nullptr, nullptr, "FxaaPS", "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        OutputDebugStringA("[GhostX] FXAA shader failed to compile: FXAA is unavailable.\n");
        return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = f.rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.InputLayout = { nullptr, 0 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;            // the destination: swap chain back buffer or offscreen target
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;
    if (FAILED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&f.pso)))) return GHOST_FAIL;

    // ---- heaps: one RTV and one SRV per surface ----
    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_SURFACES;
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_SURFACES;
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&f.rtvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&f.srvHeap))))
        return GHOST_FAIL;
    f.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    f.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    f.ready = true;
    return GHOST_OK;
}

void GhostFXAA_Release(GhostFXAA& f, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    f.ldr[id].Reset();
    f.w[id] = f.h[id] = 0;
}

void GhostFXAA_Shutdown(GhostFXAA& f)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostFXAA_Release(f, i);
    f.pso.Reset();
    f.rootSig.Reset();
    f.rtvHeap.Reset();
    f.srvHeap.Reset();
    f.ready = false;
}

GhostResult GhostFXAA_Ensure(IGhostRHI* rhi, GhostFXAA& f, int id, unsigned int w, unsigned int h)
{
    if (!f.ready || id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (f.ldr[id] && f.w[id] == w && f.h[id] == h) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    GhostFXAA_Release(f, id);                                // the GPU is idle between frames

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> l;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&l))))
        return GHOST_FAIL;

    dev->CreateRenderTargetView(l.Get(), nullptr, FHandle(f.rtvHeap.Get(), (unsigned int)id, f.rtvInc));
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    dev->CreateShaderResourceView(l.Get(), &sv, FHandle(f.srvHeap.Get(), (unsigned int)id, f.srvInc));

    f.ldr[id] = l;
    f.w[id] = w;
    f.h[id] = h;
    return GHOST_OK;
}

// ============================================================
//  Frame
// ============================================================
D3D12_CPU_DESCRIPTOR_HANDLE GhostFXAA_LdrRTV(GhostFXAA& f, int id)
{
    return FHandle(f.rtvHeap.Get(), (unsigned int)id, f.rtvInc);
}

void GhostFXAA_BeginLdr(IGhostRHI* rhi, GhostFXAA& f, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !f.ldr[id]) return;
    D3D12_RESOURCE_BARRIER b = FTrans(f.ldr[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rhi->GetCmdList()->ResourceBarrier(1, &b);
}

void GhostFXAA_Run(IGhostRHI* rhi, GhostFXAA& f, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest, unsigned int w, unsigned int h)
{
    if (!f.ready || id < 0 || id >= GHOST_MAX_SURFACES || !f.ldr[id]) return;
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = FTrans(f.ldr[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &b);

    cmd->OMSetRenderTargets(1, &dest, FALSE, nullptr);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { f.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(f.pso.Get());
    cmd->SetGraphicsRootSignature(f.rootSig.Get());
    D3D12_GPU_DESCRIPTOR_HANDLE g = f.srvHeap->GetGPUDescriptorHandleForHeapStart();
    g.ptr += (UINT64)id * f.srvInc;
    cmd->SetGraphicsRootDescriptorTable(0, g);

    // quality presets: { subpixel, relative threshold, minimum threshold, search steps }
    static const float kQ[3][4] = { { 0.50f, 0.250f, 0.0833f, 6.0f },
                                    { 0.75f, 0.166f, 0.0833f, 12.0f },
                                    { 1.00f, 0.125f, 0.0625f, 16.0f } };
    const int q = f.quality < 0 ? 0 : (f.quality > 2 ? 2 : f.quality);
    const float k[8] = { 1.0f / (float)w, 1.0f / (float)h, kQ[q][0], kQ[q][1], kQ[q][2], kQ[q][3], 0.0f, 0.0f };
    cmd->SetGraphicsRoot32BitConstants(1, 8, k, 0);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}