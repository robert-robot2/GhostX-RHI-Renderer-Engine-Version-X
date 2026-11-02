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
    return GHOST_OK;
}

void GhostMSAA_Shutdown(GhostMSAA& m)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostMSAA_Release(m, i);
    m.rtvHeap.Reset();
    m.dsvHeap.Reset();
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

    // ---- depth ----
    rd.Format = GHOST_MSAA_DEPTH_FORMAT;
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

void GhostMSAA_End(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id, bool intoOffscreen)
{
    p->rtvOverrideActive = false;
    p->dsvOverrideActive = false;
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m.color[id]) return;

    if (!intoOffscreen)
    {
        rhi->ResolveToBackBuffer(m.color[id].Get(), GHOST_MSAA_COLOR_FORMAT);
        return;
    }

    // The offscreen target is in RENDER_TARGET state here (GhostBeginOffscreen put it there)
    ID3D12Resource* dst = p->offTargets[id].Get();
    if (!dst) return;
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
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    cmd->ResourceBarrier(2, b);

    cmd->ResolveSubresource(dst, 0, m.color[id].Get(), 0, GHOST_MSAA_COLOR_FORMAT);

    b[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    b[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RESOLVE_DEST;
    b[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd->ResourceBarrier(2, b);
}