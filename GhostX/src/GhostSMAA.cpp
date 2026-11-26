// GhostSMAA.cpp
#include "../include/GhostSMAA.h"
#include "../include/AreaTex.h"
#include "../include/SearchTex.h"
#include <d3dcompiler.h>
#include <string.h>
#include <string>

static_assert(sizeof(areaTexBytes) == (size_t)AREATEX_SIZE, "AreaTex.h: unexpected size");
static_assert(sizeof(searchTexBytes) == (size_t)SEARCHTEX_SIZE, "SearchTex.h: unexpected size");

// Per surface the SRV heap holds 9 descriptors, laid out so each pass binds one contiguous table of 3
// (unused entries are null descriptors, so nothing ever references a resource that is being rendered to):
//   edge pass   (slot 0): ldr, null, null
//   weight pass (slot 3): edges, area, search
//   blend pass  (slot 6): ldr, blend, null
static const unsigned int kSlots = 9;
static const unsigned int SL_LDR_A = 0, SL_NULL_A = 1, SL_NULL_B = 2, SL_EDGES = 3, SL_AREA = 4, SL_SEARCH = 5,
SL_LDR_B = 6, SL_BLEND = 7, SL_NULL_C = 8;

static D3D12_CPU_DESCRIPTOR_HANDLE SHandle(ID3D12DescriptorHeap* heap, unsigned int index, unsigned int inc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)index * inc;
    return h;
}

static D3D12_RESOURCE_BARRIER STrans(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

static bool CompileSmaa(const std::wstring& path, const D3D_SHADER_MACRO* mac, const char* entry, const char* target, ID3DBlob** out)
{
    ComPtr<ID3DBlob> err;
    const HRESULT hr = D3DCompileFromFile(path.c_str(), mac, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &err);
    if (FAILED(hr))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        else OutputDebugStringA("[GhostX] SMAA: could not open shaders\\GhostSMAA.hlsl (or SMAA.hlsl next to it)\n");
        return false;
    }
    return true;
}

static bool BuildPsos(ID3D12Device* dev, GhostSMAA& s, int q)
{
    static const char* kPreset[4] = { "SMAA_PRESET_LOW", "SMAA_PRESET_MEDIUM", "SMAA_PRESET_HIGH", "SMAA_PRESET_ULTRA" };
    q = q < 0 ? 0 : (q > 3 ? 3 : q);
    const D3D_SHADER_MACRO mac[2] = { { kPreset[q], "1" },{ nullptr, nullptr } };
    const std::wstring path = GhostShaderPath(L"GhostSMAA.hlsl");

    ComPtr<ID3DBlob> evs, eps, wvs, wps, bvs, bps;
    if (!CompileSmaa(path, mac, "EdgeVS", "vs_5_0", &evs) || !CompileSmaa(path, mac, "EdgePS", "ps_5_0", &eps) ||
        !CompileSmaa(path, mac, "WeightVS", "vs_5_0", &wvs) || !CompileSmaa(path, mac, "WeightPS", "ps_5_0", &wps) ||
        !CompileSmaa(path, mac, "BlendVS", "vs_5_0", &bvs) || !CompileSmaa(path, mac, "BlendPS", "ps_5_0", &bps))
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
    d.pRootSignature = s.rootSig.Get();
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

    auto make = [&](ID3DBlob* vs, ID3DBlob* ps, DXGI_FORMAT fmt, ComPtr<ID3D12PipelineState>& out) -> bool
        {
            D3D12_GRAPHICS_PIPELINE_STATE_DESC x = d;
            x.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            x.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            x.RTVFormats[0] = fmt;
            return SUCCEEDED(dev->CreateGraphicsPipelineState(&x, IID_PPV_ARGS(&out)));
        };
    ComPtr<ID3D12PipelineState> pe, pw, pb;
    if (!make(evs.Get(), eps.Get(), DXGI_FORMAT_R8G8_UNORM, pe) ||
        !make(wvs.Get(), wps.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, pw) ||
        !make(bvs.Get(), bps.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, pb))
        return false;

    s.edgePso = pe;                       // the GPU is idle between frames, so the old ones can go
    s.weightPso = pw;
    s.blendPso = pb;
    s.builtQuality = q;
    return true;
}

// Uploads a small 2D texture (the SMAA lookup tables). Rests in PIXEL_SHADER_RESOURCE.
static bool UploadTexture(IGhostRHI* rhi, DXGI_FORMAT fmt, UINT w, UINT h, const unsigned char* src, UINT srcPitch,
    ComPtr<ID3D12Resource>& out)
{
    ID3D12Device* dev = rhi->GetDevice();
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w;
    td.Height = h;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = fmt;
    td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> tex;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex))))
        return false;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &rowBytes, &total);

    D3D12_HEAP_PROPERTIES up = {};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC ud = {};
    ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ud.Width = total;
    ud.Height = 1;
    ud.DepthOrArraySize = 1;
    ud.MipLevels = 1;
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.SampleDesc.Count = 1;
    ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> upload;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
        return false;

    unsigned char* dst = nullptr;
    D3D12_RANGE rr = { 0, 0 };
    if (FAILED(upload->Map(0, &rr, (void**)&dst)) || !dst) return false;
    for (UINT r = 0; r < rows; r++)
        memcpy(dst + fp.Offset + (UINT64)r * fp.Footprint.RowPitch, src + (size_t)r * srcPitch, (size_t)rowBytes);
    upload->Unmap(0, nullptr);

    ID3D12Resource* texPtr = tex.Get();
    ID3D12Resource* upPtr = upload.Get();
    const GhostResult r = rhi->ExecuteOneShot([&](ID3D12GraphicsCommandList* cmd)
        {
            D3D12_TEXTURE_COPY_LOCATION dl = {}, sl = {};
            dl.pResource = texPtr;
            dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dl.SubresourceIndex = 0;
            sl.pResource = upPtr;
            sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            sl.PlacedFootprint = fp;
            cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
            D3D12_RESOURCE_BARRIER b = STrans(texPtr, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &b);
        });
    if (r != GHOST_OK) return false;
    out = tex;
    return true;
}

// ============================================================
//  Lifecycle
// ============================================================
GhostResult GhostSMAA_Init(IGhostRHI* rhi, GhostSMAA& s)
{
    s.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return GHOST_FAIL;

    // ---- root signature: one table of 3 SRVs (t0..t2), 4 root constants (the target metrics, b0), two static samplers ----
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
    rp[1].Constants.Num32BitValues = 4;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;            // the vertex shaders compute offsets from the metrics too

    D3D12_STATIC_SAMPLER_DESC ss[2] = {};                            // s0 = LinearSampler, s1 = PointSampler (see SMAA.hlsl)
    for (int i = 0; i < 2; i++)
    {
        ss[i].Filter = (i == 0) ? D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
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
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
        FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s.rootSig))))
        return GHOST_FAIL;

    // ---- heaps ----
    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = GHOST_MAX_SURFACES * 3;                      // per surface: ldr, edges, blend
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    D3D12_DESCRIPTOR_HEAP_DESC sh = {};
    sh.NumDescriptors = GHOST_MAX_SURFACES * kSlots;
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&s.rtvHeap))) ||
        FAILED(dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&s.srvHeap))))
        return GHOST_FAIL;
    s.rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s.srvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // ---- lookup tables ----
    if (!UploadTexture(rhi, DXGI_FORMAT_R8G8_UNORM, AREATEX_WIDTH, AREATEX_HEIGHT, areaTexBytes, AREATEX_PITCH, s.area) ||
        !UploadTexture(rhi, DXGI_FORMAT_R8_UNORM, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, searchTexBytes, SEARCHTEX_PITCH, s.search))
        return GHOST_FAIL;

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    for (unsigned int id = 0; id < GHOST_MAX_SURFACES; id++)
    {
        const unsigned int base = id * kSlots;
        sv.Format = DXGI_FORMAT_R8G8_UNORM;
        dev->CreateShaderResourceView(s.area.Get(), &sv, SHandle(s.srvHeap.Get(), base + SL_AREA, s.srvInc));
        sv.Format = DXGI_FORMAT_R8_UNORM;
        dev->CreateShaderResourceView(s.search.Get(), &sv, SHandle(s.srvHeap.Get(), base + SL_SEARCH, s.srvInc));
        sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        for (unsigned int n : { SL_NULL_A, SL_NULL_B, SL_NULL_C })
            dev->CreateShaderResourceView(nullptr, &sv, SHandle(s.srvHeap.Get(), base + n, s.srvInc));
    }

    // ---- shaders. Not fatal for the engine: without them SMAA just stays unavailable ----
    if (!BuildPsos(dev, s, s.quality))
    {
        OutputDebugStringA("[GhostX] SMAA shaders failed to compile: SMAA is unavailable.\n");
        return GHOST_FAIL;
    }
    s.ready = true;
    return GHOST_OK;
}

void GhostSMAA_Release(GhostSMAA& s, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    s.ldr[id].Reset();
    s.edges[id].Reset();
    s.blend[id].Reset();
    s.w[id] = s.h[id] = 0;
}

void GhostSMAA_Shutdown(GhostSMAA& s)
{
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostSMAA_Release(s, i);
    s.area.Reset();
    s.search.Reset();
    s.edgePso.Reset(); s.weightPso.Reset(); s.blendPso.Reset();
    s.rootSig.Reset();
    s.rtvHeap.Reset(); s.srvHeap.Reset();
    s.ready = false;
    s.builtQuality = -1;
}

GhostResult GhostSMAA_SetQuality(IGhostRHI* rhi, GhostSMAA& s, int q)
{
    q = q < 0 ? 0 : (q > 3 ? 3 : q);
    s.quality = q;                                                   // set even on failure, so a broken preset isn't retried every frame
    if (!s.ready || !rhi) return GHOST_FAIL;
    if (q == s.builtQuality) return GHOST_OK;
    return BuildPsos(rhi->GetDevice(), s, q) ? GHOST_OK : GHOST_FAIL;
}

GhostResult GhostSMAA_Ensure(IGhostRHI* rhi, GhostSMAA& s, int id, unsigned int w, unsigned int h)
{
    if (!s.ready || id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (s.ldr[id] && s.edges[id] && s.blend[id] && s.w[id] == w && s.h[id] == h) return GHOST_OK;

    ID3D12Device* dev = rhi->GetDevice();
    GhostSMAA_Release(s, id);                                        // the GPU is idle between frames

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
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ComPtr<ID3D12Resource> l, e, b;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&l))) ||
        FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&b))))
        return GHOST_FAIL;
    rd.Format = DXGI_FORMAT_R8G8_UNORM;
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DXGI_FORMAT_R8G8_UNORM;                              // edges are always cleared to 0
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&e))))
        return GHOST_FAIL;

    const unsigned int rb = (unsigned int)id * 3, sb = (unsigned int)id * kSlots;
    dev->CreateRenderTargetView(l.Get(), nullptr, SHandle(s.rtvHeap.Get(), rb + 0, s.rtvInc));
    dev->CreateRenderTargetView(e.Get(), nullptr, SHandle(s.rtvHeap.Get(), rb + 1, s.rtvInc));
    dev->CreateRenderTargetView(b.Get(), nullptr, SHandle(s.rtvHeap.Get(), rb + 2, s.rtvInc));

    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    dev->CreateShaderResourceView(l.Get(), &sv, SHandle(s.srvHeap.Get(), sb + SL_LDR_A, s.srvInc));
    dev->CreateShaderResourceView(l.Get(), &sv, SHandle(s.srvHeap.Get(), sb + SL_LDR_B, s.srvInc));
    dev->CreateShaderResourceView(b.Get(), &sv, SHandle(s.srvHeap.Get(), sb + SL_BLEND, s.srvInc));
    sv.Format = DXGI_FORMAT_R8G8_UNORM;
    dev->CreateShaderResourceView(e.Get(), &sv, SHandle(s.srvHeap.Get(), sb + SL_EDGES, s.srvInc));

    s.ldr[id] = l;
    s.edges[id] = e;
    s.blend[id] = b;
    s.w[id] = w;
    s.h[id] = h;
    return GHOST_OK;
}

// ============================================================
//  Frame
// ============================================================
D3D12_CPU_DESCRIPTOR_HANDLE GhostSMAA_LdrRTV(GhostSMAA& s, int id)
{
    return SHandle(s.rtvHeap.Get(), (unsigned int)id * 3, s.rtvInc);
}

void GhostSMAA_BeginLdr(IGhostRHI* rhi, GhostSMAA& s, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !s.ldr[id]) return;
    D3D12_RESOURCE_BARRIER b = STrans(s.ldr[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rhi->GetCmdList()->ResourceBarrier(1, &b);
}

void GhostSMAA_Run(IGhostRHI* rhi, GhostSMAA& s, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest, unsigned int w, unsigned int h)
{
    if (!s.ready || id < 0 || id >= GHOST_MAX_SURFACES || !s.ldr[id] || !s.edges[id] || !s.blend[id]) return;
    auto* cmd = rhi->GetCmdList();
    const unsigned int rb = (unsigned int)id * 3, sb = (unsigned int)id * kSlots;

    D3D12_RESOURCE_BARRIER b = STrans(s.ldr[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &b);

    ID3D12DescriptorHeap* heaps[] = { s.srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(s.rootSig.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    const float metrics[4] = { 1.0f / (float)w, 1.0f / (float)h, (float)w, (float)h };
    cmd->SetGraphicsRoot32BitConstants(1, 4, metrics, 0);

    auto table = [&](unsigned int slot)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE g = s.srvHeap->GetGPUDescriptorHandleForHeapStart();
            g.ptr += (UINT64)(sb + slot) * s.srvInc;
            cmd->SetGraphicsRootDescriptorTable(0, g);
        };
    const float zero[4] = { 0, 0, 0, 0 };

    // 1. edges (cleared first: the pixel shader discards where there is no edge)
    b = STrans(s.edges[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &b);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = SHandle(s.rtvHeap.Get(), rb + 1, s.rtvInc);
    cmd->ClearRenderTargetView(rtv, zero, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cmd->SetPipelineState(s.edgePso.Get());
    table(SL_LDR_A);
    cmd->DrawInstanced(3, 1, 0, 0);
    b = STrans(s.edges[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &b);

    // 2. blend weights
    b = STrans(s.blend[id].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &b);
    rtv = SHandle(s.rtvHeap.Get(), rb + 2, s.rtvInc);
    cmd->ClearRenderTargetView(rtv, zero, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cmd->SetPipelineState(s.weightPso.Get());
    table(SL_EDGES);
    cmd->DrawInstanced(3, 1, 0, 0);
    b = STrans(s.blend[id].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &b);

    // 3. neighborhood blending into the destination
    cmd->OMSetRenderTargets(1, &dest, FALSE, nullptr);
    cmd->SetPipelineState(s.blendPso.Get());
    table(SL_LDR_B);
    cmd->DrawInstanced(3, 1, 0, 0);
}