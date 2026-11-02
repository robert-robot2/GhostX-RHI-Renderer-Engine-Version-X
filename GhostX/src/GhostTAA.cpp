// GhostTAA.cpp
#include "../include/GhostTAA.h"
#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

// ============================================================
//  Resolve shader (embedded, so there is no file to deploy)
// ============================================================
static const char* kTaaHLSL = R"HLSL(
Texture2D<float4> gCur   : register(t0);
Texture2D<float4> gHist  : register(t1);
Texture2D<float>  gDepth : register(t2);
SamplerState sLin : register(s0);
SamplerState sPt  : register(s1);

cbuffer TaaCB : register(b0)
{
    float4 eyeP;       // xyz eye
    float4 camR;       // xyz right,   w = proj[0][0]
    float4 camU;       // xyz up,      w = proj[1][1]
    float4 camF;       // xyz forward, w = proj[2][2]
    float4 depthP;     // x = proj[2][3]
    float4 tune;       // x feedback still, y feedback moving, z sharpen, w reset (>0.5 = ignore history)
    float4 size;       // x width, y height, z clip gamma, w velocity scale
    float4 padA;
    row_major float4x4 prevVP;
};

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut TaaVS(uint vid : SV_VertexID)
{
    float2 c = float2((vid << 1) & 2, vid & 2);          // (0,0) (2,0) (0,2)
    VSOut o;
    o.pos = float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
    o.uv  = float2(c.x, 1.0f - c.y);
    return o;
}

float3 RGB2YCoCg(float3 c)
{
    return float3(0.25f * c.r + 0.5f * c.g + 0.25f * c.b,
                  0.5f  * c.r               - 0.5f  * c.b,
                 -0.25f * c.r + 0.5f * c.g - 0.25f * c.b);
}
float3 YCoCg2RGB(float3 c)
{
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

struct PSOut { float4 hist : SV_Target0; float4 disp : SV_Target1; };

PSOut TaaPS(VSOut i)
{
    int2   dim = int2(size.xy);
    int2   px  = clamp(int2(i.pos.xy), int2(0, 0), dim - 1);
    float2 uv  = (float2(px) + 0.5f) / size.xy;

    // ---- 3x3 neighbourhood: color statistics + the closest depth ----
    float3 m1 = float3(0, 0, 0), m2 = float3(0, 0, 0), cur = float3(0, 0, 0);
    float  nd = 2.0f;
    int2   np = px;
    [unroll] for (int y = -1; y <= 1; y++)
    {
        [unroll] for (int x = -1; x <= 1; x++)
        {
            int2   q  = clamp(px + int2(x, y), int2(0, 0), dim - 1);
            float3 c  = gCur.Load(int3(q, 0)).rgb;
            float3 yc = RGB2YCoCg(c);
            m1 += yc;
            m2 += yc * yc;
            if (x == 0 && y == 0) cur = c;
            float d = gDepth.Load(int3(q, 0));
            if (d < nd) { nd = d; np = q; }
        }
    }

    // ---- reproject: depth -> view position -> world -> previous frame's screen ----
    float  vz  = depthP.x / (nd - camF.w);
    float2 nuv = (float2(np) + 0.5f) / size.xy;
    float2 ndc = float2(nuv.x * 2.0f - 1.0f, 1.0f - nuv.y * 2.0f);
    float3 wp  = eyeP.xyz + camR.xyz * (ndc.x * vz / camR.w)
                          + camU.xyz * (ndc.y * vz / camU.w)
                          + camF.xyz * vz;
    float4 pc  = mul(prevVP, float4(wp, 1.0f));

    bool   ok  = pc.w > 1e-4f;
    float2 vel = float2(0, 0);
    float2 huv = uv;
    if (ok)
    {
        float2 pn  = pc.xy / pc.w;
        float2 puv = float2(pn.x * 0.5f + 0.5f, 0.5f - pn.y * 0.5f);
        vel = nuv - puv;
        huv = uv - vel;
    }
    ok = ok && all(huv >= float2(0, 0)) && all(huv <= float2(1, 1)) && tune.w < 0.5f;

    // ---- history: variance clip toward the neighbourhood mean, motion-dependent blend ----
    float3 res = cur;
    if (ok)
    {
        float3 h    = gHist.SampleLevel(sLin, huv, 0).rgb;          // bilinear on purpose: soft and gummy
        float3 mean = m1 / 9.0f;
        float3 sig  = sqrt(max(m2 / 9.0f - mean * mean, 0.0f));
        float3 ext  = max(sig * size.z, float3(0.004f, 0.004f, 0.004f));
        float3 hy   = RGB2YCoCg(h);
        float3 v    = hy - mean;
        float3 a    = abs(v / ext);
        float  mx   = max(a.x, max(a.y, a.z));
        if (mx > 1.0f) hy = mean + v / mx;

        float velPx = length(vel * size.xy);
        float fb    = lerp(tune.x, tune.y, saturate(velPx * size.w));
        res = lerp(cur, YCoCg2RGB(hy), fb);
    }

    // ---- output-only sharpen (luma), kept out of the history ----
    float meanY = m1.x / 9.0f;
    float dY    = clamp(tune.z * (RGB2YCoCg(res).x - meanY), -0.08f, 0.08f);

    PSOut o;
    o.hist = float4(res, 1.0f);
    o.disp = float4(saturate(res + dY), 1.0f);
    return o;
}
)HLSL";

// ============================================================
//  Helpers
// ============================================================
static D3D12_CPU_DESCRIPTOR_HANDLE HandleAt(ID3D12DescriptorHeap* heap, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * inc;
    return h;
}

static D3D12_RESOURCE_BARRIER Trans(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static float Halton(unsigned int i, unsigned int base)
{
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= (float)base; r += f * (float)(i % base); i /= base; }
    return r;
}

// ============================================================
//  Lifecycle
// ============================================================
GhostResult GhostTAA_Init(IGhostRHI* rhi, GhostTAA& t)
{
    t.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return GHOST_FAIL;

    // ---- root signature: t0..t2 table + 48 root constants + two static samplers ----
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 3;
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
    rp[1].Constants.Num32BitValues = GHOST_TAA_CONSTS;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC ss[2] = {};
    for (int i = 0; i < 2; i++)
    {
        ss[i].Filter = (i == 0) ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
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
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr)) ||
        FAILED(dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&t.rootSig))))
        return GHOST_FAIL;

    // ---- shaders (compiled from the embedded string) ----
    ComPtr<ID3DBlob> vs, ps, err;
    const size_t len = strlen(kTaaHLSL);
    if (FAILED(D3DCompile(kTaaHLSL, len, "GhostTAA", nullptr, nullptr, "TaaVS", "vs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }
    err.Reset();
    if (FAILED(D3DCompile(kTaaHLSL, len, "GhostTAA", nullptr, nullptr, "TaaPS", "ps_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }

    // ---- PSO: two render targets (history RGBA16F + destination RGBA8), no depth ----
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = t.rootSig.Get();
    d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    d.InputLayout = { nullptr, 0 };
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 2;
    d.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    d.RTVFormats[1] = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DSVFormat = DXGI_FORMAT_UNKNOWN;
    if (FAILED(dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&t.pso)))) return GHOST_FAIL;

    // ---- heaps: RTV 3 per surface (scene, hist0, hist1), DSV 1, SRV 6 (two sets of {scene, hist, depth}) ----
    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_SURFACES * 3;
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC dh = {};
    dh.NumDescriptors = GHOST_MAX_SURFACES;
    dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_SURFACES * 6;
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&t.rtvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&t.dsvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&t.srvHeap))))
        return GHOST_FAIL;
    t.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    t.dsvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    t.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    t.ready = true;
    return GHOST_OK;
}

void GhostTAA_Release(GhostTAA& t, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    t.scene[id].Reset();
    t.hist[0][id].Reset();
    t.hist[1][id].Reset();
    t.depth[id].Reset();
    t.w[id] = t.h[id] = 0;
    t.read[id] = 0;
    t.reset[id] = true;
    t.havePrev[id] = false;
}

void GhostTAA_Shutdown(GhostTAA& t)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostTAA_Release(t, i);
    t.pso.Reset();
    t.rootSig.Reset();
    t.rtvHeap.Reset();
    t.dsvHeap.Reset();
    t.srvHeap.Reset();
    t.ready = false;
}

GhostResult GhostTAA_Ensure(IGhostRHI* rhi, GhostTAA& t, int id, unsigned int w, unsigned int h)
{
    if (!t.ready || id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (t.scene[id] && t.depth[id] && t.hist[0][id] && t.hist[1][id] && t.w[id] == w && t.h[id] == h) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    GhostTAA_Release(t, id);

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> sc, h0, h1, dp;

    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&sc)))) return GHOST_FAIL;

    rd.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&h0)))) return GHOST_FAIL;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&h1)))) return GHOST_FAIL;

    rd.Format = DXGI_FORMAT_R24G8_TYPELESS;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&dp)))) return GHOST_FAIL;

    // ---- RTVs / DSV ----
    dev->CreateRenderTargetView(sc.Get(), nullptr, HandleAt(t.rtvHeap.Get(), id * 3 + 0, t.rtvInc));
    dev->CreateRenderTargetView(h0.Get(), nullptr, HandleAt(t.rtvHeap.Get(), id * 3 + 1, t.rtvInc));
    dev->CreateRenderTargetView(h1.Get(), nullptr, HandleAt(t.rtvHeap.Get(), id * 3 + 2, t.rtvInc));

    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dev->CreateDepthStencilView(dp.Get(), &dv, HandleAt(t.dsvHeap.Get(), id, t.dsvInc));

    // ---- SRVs: set 0 = {scene, hist0, depth}, set 1 = {scene, hist1, depth} ----
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    ID3D12Resource* histRes[2] = { h0.Get(), h1.Get() };
    for (int s = 0; s < 2; s++)
    {
        const unsigned int b = (unsigned int)id * 6 + (unsigned int)s * 3;
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dev->CreateShaderResourceView(sc.Get(), &sv, HandleAt(t.srvHeap.Get(), b + 0, t.srvInc));
        sv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        dev->CreateShaderResourceView(histRes[s], &sv, HandleAt(t.srvHeap.Get(), b + 1, t.srvInc));
        sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        dev->CreateShaderResourceView(dp.Get(), &sv, HandleAt(t.srvHeap.Get(), b + 2, t.srvInc));
    }

    t.scene[id] = sc;
    t.hist[0][id] = h0;
    t.hist[1][id] = h1;
    t.depth[id] = dp;
    t.w[id] = w;
    t.h[id] = h;
    t.read[id] = 0;
    t.reset[id] = true;          // first frame: no history yet
    t.havePrev[id] = false;
    return GHOST_OK;
}

// ============================================================
//  Jitter
// ============================================================
void GhostTAA_NextJitter(GhostTAA& t, int id, float speed, float& jx, float& jy)
{
    jx = jy = 0.0f;
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    if (!(speed > 0.05f)) speed = 0.05f;

    const unsigned int idx = ((unsigned int)floorf(t.phase[id])) % 16u;
    t.phase[id] = fmodf(t.phase[id] + speed, 16.0f);          // the index only depends on the phase modulo 16
    jx = Halton(idx + 1, 2) - 0.5f;
    jy = Halton(idx + 1, 3) - 0.5f;
}

// ============================================================
//  Begin / End
// ============================================================
void GhostTAA_Begin(IGhostRHI* rhi, GhostPipeline* p, GhostTAA& t, int id,
    const GhostColor* clear, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !t.scene[id] || !t.depth[id]) return;
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = Trans(t.scene[id].Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &b);                                  // the depth buffer rests in DEPTH_WRITE

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = HandleAt(t.rtvHeap.Get(), id * 3 + 0, t.rtvInc);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = HandleAt(t.dsvHeap.Get(), id, t.dsvInc);

    const float c[4] = { clear->r, clear->g, clear->b, clear->a };
    cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    // Every draw function already honors these overrides (same mechanism MSAA uses)
    p->rtvOverride = rtv;  p->rtvOverrideActive = true;
    p->dsvOverride = dsv;  p->dsvOverrideActive = true;
}

void GhostTAA_End(IGhostRHI* rhi, GhostPipeline* p, GhostTAA& t, int id,
    D3D12_CPU_DESCRIPTOR_HANDLE dest, bool destIsOffscreen, const GhostTAAFrame& f)
{
    p->dsvOverrideActive = false;
    if (id < 0 || id >= GHOST_MAX_SURFACES || !t.scene[id] || !t.depth[id]) { p->rtvOverrideActive = false; return; }
    auto* cmd = rhi->GetCmdList();

    const int rd = t.read[id];            // history written last frame
    const int wr = rd ^ 1;                // history written this frame
    ID3D12Resource* histW = t.hist[wr][id].Get();

    // ---- scene + depth become readable, the new history becomes writable ----
    D3D12_RESOURCE_BARRIER b[3];
    b[0] = Trans(t.scene[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    b[1] = Trans(t.depth[id].Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    b[2] = Trans(histW, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(3, b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = { HandleAt(t.rtvHeap.Get(), id * 3 + 1 + wr, t.rtvInc), dest };
    cmd->OMSetRenderTargets(2, rtvs, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)t.w[id], (float)t.h[id], 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)t.w[id], (LONG)t.h[id] };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { t.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(t.pso.Get());
    cmd->SetGraphicsRootSignature(t.rootSig.Get());

    D3D12_GPU_DESCRIPTOR_HANDLE tbl = t.srvHeap->GetGPUDescriptorHandleForHeapStart();
    tbl.ptr += (UINT64)((unsigned int)id * 6 + (unsigned int)rd * 3) * t.srvInc;
    cmd->SetGraphicsRootDescriptorTable(0, tbl);

    const bool noHistory = t.reset[id] || !t.havePrev[id];
    if (!t.havePrev[id]) memcpy(t.prevVP[id], f.vp, sizeof(float) * 16);

    float k[GHOST_TAA_CONSTS] = {};
    k[0] = f.eye[0];   k[1] = f.eye[1];   k[2] = f.eye[2];
    k[4] = f.right[0]; k[5] = f.right[1]; k[6] = f.right[2];  k[7] = f.p00;
    k[8] = f.up[0];    k[9] = f.up[1];    k[10] = f.up[2];    k[11] = f.p11;
    k[12] = f.fwd[0];  k[13] = f.fwd[1];  k[14] = f.fwd[2];   k[15] = f.p22;
    k[16] = f.p23;
    k[20] = t.feedbackStill;  k[21] = t.feedbackMoving;  k[22] = t.sharpen;  k[23] = noHistory ? 1.0f : 0.0f;
    k[24] = (float)t.w[id];   k[25] = (float)t.h[id];    k[26] = t.clipGamma; k[27] = t.velocityScale;
    memcpy(k + 32, t.prevVP[id], sizeof(float) * 16);
    cmd->SetGraphicsRoot32BitConstants(1, GHOST_TAA_CONSTS, k, 0);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    // ---- history readable again, depth back to its resting state ----
    b[0] = Trans(histW, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    b[1] = Trans(t.depth[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmd->ResourceBarrier(2, b);

    memcpy(t.prevVP[id], f.vp, sizeof(float) * 16);
    t.havePrev[id] = true;
    t.reset[id] = false;
    t.read[id] = wr;

    // The game UI (and the rest of the frame) draws onto the destination from here
    p->rtvOverride = dest;
    p->rtvOverrideActive = destIsOffscreen;
}