// GhostLightShafts.cpp - put in src/
#include "../include/GhostLightShafts.h"
#include <d3dcompiler.h>
#include <string>
#include <string.h>

// ---------- shader path (same rule as GhostPipeline.cpp: shaders\ next to the DLL) ----------
static std::wstring ShaftModuleDir()
{
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&ShaftModuleDir, &h);
    wchar_t buf[1024];
    DWORD n = GetModuleFileNameW(h, buf, 1024);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash + 1);
}

static std::wstring ShaftShaderPath(const wchar_t* file)
{
    std::wstring a = ShaftModuleDir() + L"shaders\\" + file;
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
    return std::wstring(L"shaders/") + file;
}

static bool ShaftCompile(const wchar_t* path, const char* entry, const char* target, ID3DBlob** out)
{
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompileFromFile(path, nullptr, nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, out, &err);
    if (FAILED(hr))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    return true;
}

static D3D12_RESOURCE_BARRIER ShaftTr(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER x = {};
    x.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    x.Transition.pResource = r;
    x.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    x.Transition.StateBefore = a;
    x.Transition.StateAfter = b;
    return x;
}

struct ShaftCB { float tint[4], sun[4], occl[4], proj[4], misc[4]; };   // 80 bytes, matches cbuffer ShaftCB
static const unsigned int kSlot = 256;                                  // constant buffer slot size

// ---------- init ----------
GhostResult GhostShafts_Init(IGhostRHI* rhi, GhostShafts& s)
{
    s.ready = false;
    if (!rhi) return GHOST_FAIL;
    ID3D12Device* dev = rhi->GetDevice();

    // root signature: b0 = ShaftCB, table(t0 scene copy, t1 depth), s0 = linear clamp
    D3D12_DESCRIPTOR_RANGE rng = {};
    rng.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rng.NumDescriptors = 2;
    rng.BaseShaderRegister = 0;
    rng.RegisterSpace = 0;
    rng.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rp[2] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[1].DescriptorTable.NumDescriptorRanges = 1;
    rp[1].DescriptorTable.pDescriptorRanges = &rng;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC smp = {};
    smp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    smp.AddressU = smp.AddressV = smp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    smp.MaxAnisotropy = 1;
    smp.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    smp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    smp.MaxLOD = D3D12_FLOAT32_MAX;
    smp.ShaderRegister = 0;
    smp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &smp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return GHOST_FAIL;
    if (FAILED(dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&s.rs)))) return GHOST_FAIL;

    // shaders
    ComPtr<ID3DBlob> vs, ps;
    const std::wstring path = ShaftShaderPath(L"GhostShafts.hlsl");
    if (!ShaftCompile(path.c_str(), "VSMain", "vs_5_0", &vs) || !ShaftCompile(path.c_str(), "PSMain", "ps_5_0", &ps))
    {
        OutputDebugStringA("[GhostX] GhostShafts.hlsl failed to compile: light shafts are disabled.\n");
        return GHOST_FAIL;
    }

    // PSO: full-screen triangle, additive onto the HDR scene, no depth
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = s.rs.Get();
    pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pd.InputLayout = { nullptr, 0 };
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = GHOST_HDR_FORMAT;
    pd.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pd.SampleDesc.Count = 1;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    D3D12_RENDER_TARGET_BLEND_DESC& b = pd.BlendState.RenderTarget[0];
    b.BlendEnable = TRUE;
    b.SrcBlend = D3D12_BLEND_ONE;        b.DestBlend = D3D12_BLEND_ONE;       b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ZERO;  b.DestBlendAlpha = D3D12_BLEND_ONE;  b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN | D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (FAILED(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&s.pso)))) return GHOST_FAIL;

    // descriptor heap (shader visible): 2 per surface
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = GHOST_MAX_SURFACES * 2;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&s.heap)))) return GHOST_FAIL;
    s.inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // constant buffer: one slot per light
    D3D12_HEAP_PROPERTIES up = {};
    up.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = (UINT64)kSlot * GHOST_MAX_SHAFT_LIGHTS;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&s.cb)))) return GHOST_FAIL;
    D3D12_RANGE rr = { 0, 0 };
    s.cb->Map(0, &rr, &s.cbMapped);

    s.ready = true;
    return GHOST_OK;
}

void GhostShafts_Release(GhostShafts& s, int surface)
{
    if (surface < 0 || surface >= GHOST_MAX_SURFACES) return;
    s.copy[surface].Reset();
    s.cw[surface] = s.ch[surface] = 0;
}

void GhostShafts_Shutdown(GhostShafts& s)
{
    if (s.cbMapped && s.cb) s.cb->Unmap(0, nullptr);
    s.cbMapped = nullptr;
    for (int i = 0; i < GHOST_MAX_SURFACES; i++) GhostShafts_Release(s, i);
    s.cb.Reset(); s.heap.Reset(); s.pso.Reset(); s.rs.Reset();
    s.ready = false;
}

// ---------- draw ----------
bool GhostShafts_Draw(IGhostRHI* rhi, GhostShafts& s, int vp,
    ID3D12Resource* scene, D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV, ID3D12Resource* depth,
    unsigned int w, unsigned int h, float projA, float projB, float noise,
    const GhostShaftJob* jobs, int jobCount)
{
    if (!s.ready || !rhi || !scene || !depth || !jobs || jobCount <= 0 || w == 0 || h == 0) return false;
    if (vp < 0 || vp >= GHOST_MAX_SURFACES) return false;
    if (jobCount > (int)GHOST_MAX_SHAFT_LIGHTS) jobCount = (int)GHOST_MAX_SHAFT_LIGHTS;

    ID3D12Device* dev = rhi->GetDevice();
    auto* cmd = rhi->GetCmdList();

    // the scene copy follows the render size (the GPU is idle between frames, so replacing it is safe)
    if (!s.copy[vp] || s.cw[vp] != w || s.ch[vp] != h)
    {
        s.copy[vp].Reset();
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w; rd.Height = h; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = GHOST_HDR_FORMAT;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&s.copy[vp])))) return false;
        s.cw[vp] = w; s.ch[vp] = h;
    }
    ID3D12Resource* copy = s.copy[vp].Get();

    // descriptors for this surface: t0 = scene copy, t1 = depth
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = s.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += (SIZE_T)vp * 2 * s.inc;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    sv.Format = GHOST_HDR_FORMAT;
    dev->CreateShaderResourceView(copy, &sv, cpu);
    cpu.ptr += s.inc;
    sv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    dev->CreateShaderResourceView(depth, &sv, cpu);

    // scene -> copy, depth readable
    D3D12_RESOURCE_BARRIER b1[2] = {
        ShaftTr(scene, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
        ShaftTr(copy,  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST) };
    cmd->ResourceBarrier(2, b1);
    cmd->CopyResource(copy, scene);
    D3D12_RESOURCE_BARRIER b2[3] = {
        ShaftTr(scene, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET),
        ShaftTr(copy,  D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
        ShaftTr(depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) };
    cmd->ResourceBarrier(3, b2);

    // draw
    cmd->OMSetRenderTargets(1, &sceneRTV, FALSE, nullptr);
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &sc);

    ID3D12DescriptorHeap* heaps[] = { s.heap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootSignature(s.rs.Get());
    cmd->SetPipelineState(s.pso.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = s.heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += (UINT64)vp * 2 * s.inc;
    cmd->SetGraphicsRootDescriptorTable(1, gpu);

    for (int j = 0; j < jobCount; j++)
    {
        const GhostShaftJob& q = jobs[j];
        ShaftCB c = {};
        c.tint[0] = q.color[0]; c.tint[1] = q.color[1]; c.tint[2] = q.color[2]; c.tint[3] = q.occlusion ? 1.0f : 0.0f;
        c.sun[0] = q.sunU; c.sun[1] = q.sunV; c.sun[2] = q.length; c.sun[3] = q.decay;
        c.occl[0] = q.darkness; c.occl[1] = q.depthRange; c.occl[2] = q.threshold; c.occl[3] = q.maxBright;
        c.proj[0] = projA; c.proj[1] = projB; c.proj[2] = (float)w; c.proj[3] = (float)h;
        c.misc[0] = (float)(q.steps < 8 ? 8 : (q.steps > 128 ? 128 : q.steps)); c.misc[1] = noise;
        memcpy((char*)s.cbMapped + (size_t)j * kSlot, &c, sizeof c);
        cmd->SetGraphicsRootConstantBufferView(0, s.cb->GetGPUVirtualAddress() + (UINT64)j * kSlot);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    D3D12_RESOURCE_BARRIER b3 = ShaftTr(depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    cmd->ResourceBarrier(1, &b3);
    return true;
}