// GhostMSAA.cpp
#include "../include/GhostMSAA.h"

static bool SupportsSamples(ID3D12Device* d, DXGI_FORMAT f, UINT n)
{
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = {};
    q.Format = f;
    q.SampleCount = n;
    q.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    if (FAILED(d->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q)))) return false;
    return q.NumQualityLevels > 0;
}

GhostResult GhostMSAA_Init(IGhostRHI* rhi, GhostMSAA& m)
{
    ID3D12Device* device = rhi ? rhi->GetDevice() : nullptr;
    if (!device) return GHOST_FAIL;

    m.maxSamples = 1;
    const unsigned int tryList[3] = { 8, 4, 2 };
    for (unsigned int n : tryList)
        if (SupportsSamples(device, GHOST_MSAA_COLOR_FORMAT, n) && SupportsSamples(device, GHOST_MSAA_DEPTH_FORMAT, n))
        {
            m.maxSamples = n;
            break;
        }

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.NumDescriptors = GHOST_MAX_SURFACES;
    rd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    D3D12_DESCRIPTOR_HEAP_DESC dd = rd;
    dd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

    if (FAILED(device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&m.rtvHeap))) ||
        FAILED(device->CreateDescriptorHeap(&dd, IID_PPV_ARGS(&m.dsvHeap))))
    {
        m.maxSamples = 1;                       // no heaps: MSAA stays unavailable
        return GHOST_FAIL;
    }
    m.rtvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m.dsvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    // ---- depth resolve: multisampled depth -> 1x depth (a pixel shader writes SV_Depth) ----
    {
        static const char* kDepthHLSL = R"HLSL(
Texture2DMS<float> gDepth : register(t0);
float4 DrVS(uint vid : SV_VertexID) : SV_POSITION
{
    float2 c = float2((vid << 1) & 2, vid & 2);
    return float4(c * 2.0f - 1.0f, 0.0f, 1.0f);
}
float DrPS(float4 pos : SV_POSITION) : SV_Depth
{
    uint w, h, n;
    gDepth.GetDimensions(w, h, n);
    int2 p = int2(pos.xy);
    float d = 1.0f;
    [loop] for (uint s = 0; s < n; s++) d = min(d, gDepth.Load(p, s));
    return d;
}
)HLSL";
        D3D12_DESCRIPTOR_RANGE range = {};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER rp = {};
        rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp.DescriptorTable.NumDescriptorRanges = 1;
        rp.DescriptorTable.pDescriptorRanges = &range;
        rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsd = {};
        rsd.NumParameters = 1;
        rsd.pParameters = &rp;
        ComPtr<ID3DBlob> blob, err, vs, ps;
        const size_t len = strlen(kDepthHLSL);
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
            FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m.depthRootSig))) ||
            FAILED(D3DCompile(kDepthHLSL, len, "GhostMSAADepth", nullptr, nullptr, "DrVS", "vs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &err)) ||
            FAILED(D3DCompile(kDepthHLSL, len, "GhostMSAADepth", nullptr, nullptr, "DrPS", "ps_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &err)))
        {
            m.maxSamples = 1; return GHOST_FAIL;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
        d.pRootSignature = m.depthRootSig.Get();
        d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        d.InputLayout = { nullptr, 0 };
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 0;
        d.SampleDesc.Count = 1;
        d.SampleMask = UINT_MAX;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.RasterizerState.DepthClipEnable = TRUE;
        d.DepthStencilState.DepthEnable = TRUE;
        d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        d.DSVFormat = GHOST_MSAA_DEPTH_FORMAT;
        D3D12_DESCRIPTOR_HEAP_DESC sh = {};
        sh.NumDescriptors = GHOST_MAX_SURFACES;
        sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&m.depthPso))) ||
            FAILED(device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m.srvHeap))))
        {
            m.maxSamples = 1; return GHOST_FAIL;
        }
        m.srvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    return GHOST_OK;
}

void GhostMSAA_Shutdown(GhostMSAA& m)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostMSAA_Release(m, i);
    m.rtvHeap.Reset();
    m.dsvHeap.Reset();
    m.srvHeap.Reset(); m.depthRootSig.Reset(); m.depthPso.Reset();
    m.maxSamples = 1;
    m.active = 1;
}

unsigned int GhostMSAA_Clamp(const GhostMSAA& m, int wanted)
{
    unsigned int n = wanted < 1 ? 1u : (unsigned int)wanted;
    if (n > m.maxSamples) n = m.maxSamples;
    if (n >= 8) return 8;
    if (n >= 4) return 4;
    if (n >= 2) return 2;
    return 1;
}

GhostResult GhostMSAA_Apply(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int wanted)
{
    const unsigned int n = GhostMSAA_Clamp(m, wanted);
    if (n == m.active) return GHOST_OK;
    if (n == m.failedFor) return GHOST_FAIL;                 // already tried and failed: don't recompile shaders every frame

    if (GhostRebuildScenePSOs(rhi, p, n) != GHOST_OK)
    {
        m.failedFor = n;                                     // PSOs and targets stay as they were
        return GHOST_FAIL;
    }
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostMSAA_Release(m, i);   // old targets have the wrong sample count
    m.active = n;
    m.failedFor = 0;
    return GHOST_OK;
}

void GhostMSAA_Release(GhostMSAA& m, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    if (!m.color[id] && !m.depth[id]) return;
    m.color[id].Reset();
    m.depth[id].Reset();
    m.w[id] = m.h[id] = 0;
}

GhostResult GhostMSAA_Ensure(IGhostRHI* rhi, GhostMSAA& m, int id, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0 || m.active < 2 || !m.rtvHeap || !m.dsvHeap)
        return GHOST_FAIL;
    if (m.color[id] && m.depth[id] && m.w[id] == w && m.h[id] == h) return GHOST_OK;

    ID3D12Device* device = rhi->GetDevice();
    GhostMSAA_Release(m, id);

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w;
    rd.Height = h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = m.active;
    rd.SampleDesc.Quality = 0;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    // ---- color ----
    ComPtr<ID3D12Resource> c, d;
    rd.Format = GHOST_MSAA_COLOR_FORMAT;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&c))))
        return GHOST_FAIL;

    // ---- depth (typeless: a DSV for drawing, an SRV for the depth resolve) ----
    rd.Format = DXGI_FORMAT_R24G8_TYPELESS;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = GHOST_MSAA_DEPTH_FORMAT;
    cv.DepthStencil.Depth = 1.0f;
    cv.DepthStencil.Stencil = 0;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&d))))
        return GHOST_FAIL;

    // ---- views ----
    D3D12_RENDER_TARGET_VIEW_DESC rv = {};
    rv.Format = GHOST_MSAA_COLOR_FORMAT;
    rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    D3D12_CPU_DESCRIPTOR_HANDLE rh = m.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rh.ptr += (SIZE_T)id * m.rtvInc;
    device->CreateRenderTargetView(c.Get(), &rv, rh);

    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = GHOST_MSAA_DEPTH_FORMAT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    D3D12_CPU_DESCRIPTOR_HANDLE dh = m.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dh.ptr += (SIZE_T)id * m.dsvInc;
    device->CreateDepthStencilView(d.Get(), &dv, dh);

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    D3D12_CPU_DESCRIPTOR_HANDLE sh2 = m.srvHeap->GetCPUDescriptorHandleForHeapStart();
    sh2.ptr += (SIZE_T)id * m.srvInc;
    device->CreateShaderResourceView(d.Get(), &sv, sh2);


    m.color[id] = c;
    m.depth[id] = d;
    m.w[id] = w;
    m.h[id] = h;
    return GHOST_OK;
}

void GhostMSAA_Begin(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id,
    const GhostColor* clear, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m.color[id] || !m.depth[id]) return;
    auto* cmd = rhi->GetCmdList();

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)id * m.rtvInc;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m.dsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += (SIZE_T)id * m.dsvInc;

    const float c[4] = { clear->r, clear->g, clear->b, clear->a };
    cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    // Every draw function already honors these two overrides
    p->rtvOverride = rtv;  p->rtvOverrideActive = true;
    p->dsvOverride = dsv;  p->dsvOverrideActive = true;
}

void GhostMSAA_End(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id, ID3D12Resource* dst)
{
    p->rtvOverrideActive = false;
    p->dsvOverrideActive = false;
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m.color[id] || !dst) return;
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b[2] = {};
    b[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition.pResource = m.color[id].Get();
    b[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    b[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[1].Transition.pResource = dst;
    b[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    cmd->ResourceBarrier(2, b);

    cmd->ResolveSubresource(dst, 0, m.color[id].Get(), 0, GHOST_MSAA_COLOR_FORMAT);

    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(2, b);
}


void GhostMSAA_ResolveDepth(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m.depth[id] || !m.depthPso || !m.srvHeap || !p || !p->depthBuffers[id]) return;
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = m.depth[id].Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();   // the surface's 1x depth buffer (not the override)
    dsv.ptr += (SIZE_T)id * p->dsvIncrement;
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { m.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(m.depthPso.Get());
    cmd->SetGraphicsRootSignature(m.depthRootSig.Get());
    D3D12_GPU_DESCRIPTOR_HANDLE g = m.srvHeap->GetGPUDescriptorHandleForHeapStart();
    g.ptr += (UINT64)id * m.srvInc;
    cmd->SetGraphicsRootDescriptorTable(0, g);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cmd->ResourceBarrier(1, &b);
}