// GhostPipeline.cpp
#include "../include/GhostPipeline.h"
#include "../include/GhostMath.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

// ============================================================
//  Geometry data
// ============================================================

static GhostVertex CubeVertices[] =
{
    // Front face (normal 0,0,-1)
    { -0.5f,  0.5f, -0.5f,  1,0,0,1,  0, 0,-1,  0,0 },
    {  0.5f,  0.5f, -0.5f,  1,0,0,1,  0, 0,-1,  1,0 },
    {  0.5f, -0.5f, -0.5f,  1,0,0,1,  0, 0,-1,  1,1 },
    { -0.5f, -0.5f, -0.5f,  1,0,0,1,  0, 0,-1,  0,1 },
    // Back face (normal 0,0,1)
    {  0.5f,  0.5f,  0.5f,  0,1,0,1,  0, 0, 1,  0,0 },
    { -0.5f,  0.5f,  0.5f,  0,1,0,1,  0, 0, 1,  1,0 },
    { -0.5f, -0.5f,  0.5f,  0,1,0,1,  0, 0, 1,  1,1 },
    {  0.5f, -0.5f,  0.5f,  0,1,0,1,  0, 0, 1,  0,1 },
    // Top face (normal 0,1,0)
    { -0.5f,  0.5f,  0.5f,  0,0,1,1,  0, 1, 0,  0,0 },
    {  0.5f,  0.5f,  0.5f,  0,0,1,1,  0, 1, 0,  1,0 },
    {  0.5f,  0.5f, -0.5f,  0,0,1,1,  0, 1, 0,  1,1 },
    { -0.5f,  0.5f, -0.5f,  0,0,1,1,  0, 1, 0,  0,1 },
    // Bottom face (normal 0,-1,0)
    { -0.5f, -0.5f, -0.5f,  1,1,0,1,  0,-1, 0,  0,0 },
    {  0.5f, -0.5f, -0.5f,  1,1,0,1,  0,-1, 0,  1,0 },
    {  0.5f, -0.5f,  0.5f,  1,1,0,1,  0,-1, 0,  1,1 },
    { -0.5f, -0.5f,  0.5f,  1,1,0,1,  0,-1, 0,  0,1 },
    // Left face (normal -1,0,0)
    { -0.5f,  0.5f,  0.5f,  0,1,1,1, -1, 0, 0,  0,0 },
    { -0.5f,  0.5f, -0.5f,  0,1,1,1, -1, 0, 0,  1,0 },
    { -0.5f, -0.5f, -0.5f,  0,1,1,1, -1, 0, 0,  1,1 },
    { -0.5f, -0.5f,  0.5f,  0,1,1,1, -1, 0, 0,  0,1 },
    // Right face (normal 1,0,0)
    {  0.5f,  0.5f, -0.5f,  1,0,1,1,  1, 0, 0,  0,0 },
    {  0.5f,  0.5f,  0.5f,  1,0,1,1,  1, 0, 0,  1,0 },
    {  0.5f, -0.5f,  0.5f,  1,0,1,1,  1, 0, 0,  1,1 },
    {  0.5f, -0.5f, -0.5f,  1,0,1,1,  1, 0, 0,  0,1 },
};

static unsigned short CubeIndices[] =
{
     0, 1, 2,   0, 2, 3,   // front
     4, 5, 6,   4, 6, 7,   // back
     8, 9,10,   8,10,11,   // top
    12,13,14,  12,14,15,   // bottom
    16,17,18,  16,18,19,   // left
    20,21,22,  20,22,23,   // right
};

static GhostVertex TriangleVertices[] =
{
    {  0.0f,  1.0f,  0.0f,  1,1,1,1,  0,0,1,  0.5f, 0.0f },
    { -0.5f,  0.0f,  0.0f,  1,0,1,1,  0,0,1,  0.0f, 1.0f },
    {  0.5f,  0.0f,  0.0f,  0,1,1,1,  0,0,1,  1.0f, 1.0f },
};
static unsigned short TriangleIndices[] = { 0, 1, 2 };


static GhostVertex ShipVertices[] =
{
    {  3.0f,  0.0f,  0.0f,  0,1,0,1,   0.6f, 0.0f,-0.8f,  1.0f,0.5f },
    {  0.0f,  3.0f, -3.0f,  0,0,1,1,   0.0f, 0.8f,-0.6f,  0.5f,0.0f },
    {  0.0f,  0.0f, 10.0f,  1,0,0,1,   0.0f, 0.4f, 0.9f,  0.5f,1.0f },
    { -3.0f,  0.0f,  0.0f,  0,1,1,1,  -0.6f, 0.0f,-0.8f,  0.0f,0.5f },
    {  3.2f, -1.0f, -3.0f,  0,0,1,1,   0.7f,-0.3f,-0.6f,  1.0f,0.0f },
    {  3.2f, -1.0f, 11.0f,  0,1,0,1,   0.7f,-0.3f, 0.6f,  1.0f,1.0f },
    {  2.0f,  1.0f,  2.0f,  1,0,0,1,   0.6f, 0.6f, 0.4f,  0.8f,0.3f },
    { -3.2f, -1.0f, -3.0f,  0,0,1,1,  -0.7f,-0.3f,-0.6f,  0.0f,0.0f },
    { -3.2f, -1.0f, 11.0f,  0,1,0,1,  -0.7f,-0.3f, 0.6f,  0.0f,1.0f },
    { -2.0f,  1.0f,  2.0f,  1,0,0,1,  -0.6f, 0.6f, 0.4f,  0.2f,0.3f },
};

static unsigned short ShipIndices[] =
{
    0,1,2,  2,1,3,  3,1,0,  0,2,3,
    4,5,6,  7,8,9,
};


bool GhostGetMeshCPU(int meshId, const GhostVertex** verts,
    const unsigned short** indices, unsigned int* indexCount)
{
    switch (meshId)
    {
    case GHOST_MESH_CUBE:
        *verts = CubeVertices; *indices = CubeIndices;
        *indexCount = sizeof(CubeIndices) / sizeof(CubeIndices[0]); return true;
    case GHOST_MESH_TRIANGLE:
        *verts = TriangleVertices; *indices = TriangleIndices;
        *indexCount = sizeof(TriangleIndices) / sizeof(TriangleIndices[0]); return true;
    case GHOST_MESH_SHIP:
        *verts = ShipVertices; *indices = ShipIndices;
        *indexCount = sizeof(ShipIndices) / sizeof(ShipIndices[0]); return true;
    }
    return false;
}

// ============================================================
//  Internal helpers  (no RHI dependency — take raw device)
// ============================================================

static GhostResult CreateDepthBuffer(ID3D12Device* device,
    unsigned int width, unsigned int height,
    GhostPipeline* p)
{
    p->depthBuffer.Reset();

    D3D12_HEAP_PROPERTIES depthHeapProps = {};
    depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE depthClear = {};
    depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthClear.DepthStencil.Depth = 1.0f;
    depthClear.DepthStencil.Stencil = 0;

    HRESULT hr = device->CreateCommittedResource(
        &depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
        IID_PPV_ARGS(&p->depthBuffer));

    if (FAILED(hr)) return GHOST_FAIL;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(
        p->depthBuffer.Get(), &dsvDesc,
        p->dsvHeap->GetCPUDescriptorHandleForHeapStart());

    return GHOST_OK;
}

static GhostResult CompileShaderFromFile(const wchar_t* path,
    const char* entry,
    const char* target,
    ID3DBlob** out)
{
    ComPtr<ID3DBlob> err;
    HRESULT hr = D3DCompileFromFile(
        path, nullptr, nullptr, entry, target,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, out, &err);
    if (FAILED(hr))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return GHOST_FAIL;
    }
    return GHOST_OK;
}

static GhostResult CreateUploadBuffer(ID3D12Device* device,
    unsigned long long size,
    ID3D12Resource** out)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(out));
    return FAILED(hr) ? GHOST_FAIL : GHOST_OK;
}

// ---- Mip chain (box filter in linear space, RGBA8 sRGB in/out) ----
struct MipLevel { unsigned int w, h; std::vector<unsigned char> px; };

static float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f); }
static float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.0f / 2.4f) - 0.055f; }

static void BuildMipChain(const unsigned char* rgba, unsigned int w, unsigned int h,
    std::vector<MipLevel>& out)
{
    float lut[256];
    for (int i = 0; i < 256; i++) lut[i] = SrgbToLinear(i / 255.0f);
    unsigned char enc[4096];
    for (int i = 0; i < 4096; i++)
        enc[i] = (unsigned char)(LinearToSrgb(i / 4095.0f) * 255.0f + 0.5f);

    out.clear();
    MipLevel m0;
    m0.w = w; m0.h = h;
    m0.px.assign(rgba, rgba + (size_t)w * h * 4);
    out.push_back(std::move(m0));

    while (out.back().w > 1 || out.back().h > 1)
    {
        const MipLevel& s = out.back();
        MipLevel d;
        d.w = s.w > 1 ? s.w / 2 : 1;
        d.h = s.h > 1 ? s.h / 2 : 1;
        d.px.resize((size_t)d.w * d.h * 4);

        for (unsigned int y = 0; y < d.h; y++)
            for (unsigned int x = 0; x < d.w; x++)
            {
                unsigned int x0 = x * 2, y0 = y * 2;
                unsigned int x1 = (x0 + 1 < s.w) ? x0 + 1 : s.w - 1;
                unsigned int y1 = (y0 + 1 < s.h) ? y0 + 1 : s.h - 1;
                const unsigned char* a = &s.px[((size_t)y0 * s.w + x0) * 4];
                const unsigned char* b = &s.px[((size_t)y0 * s.w + x1) * 4];
                const unsigned char* c = &s.px[((size_t)y1 * s.w + x0) * 4];
                const unsigned char* e = &s.px[((size_t)y1 * s.w + x1) * 4];
                unsigned char* o = &d.px[((size_t)y * d.w + x) * 4];

                for (int k = 0; k < 3; k++)
                {
                    float v = (lut[a[k]] + lut[b[k]] + lut[c[k]] + lut[e[k]]) * 0.25f;
                    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                    o[k] = enc[(int)(v * 4095.0f + 0.5f)];
                }
                o[3] = (unsigned char)(((int)a[3] + b[3] + c[3] + e[3] + 2) / 4);
            }
        out.push_back(std::move(d));
    }
}


static GhostResult UploadMesh(ID3D12Device* device, GhostMesh& m,
    const void* verts, unsigned int vbSize,
    const unsigned short* idx, unsigned int ibSize)
{
    D3D12_RANGE rr = { 0, 0 };
    void* p = nullptr;

    if (CreateUploadBuffer(device, vbSize, &m.vb) != GHOST_OK) return GHOST_FAIL;
    m.vb->Map(0, &rr, &p);
    memcpy(p, verts, vbSize);
    m.vb->Unmap(0, nullptr);
    m.vbView = { m.vb->GetGPUVirtualAddress(), vbSize, sizeof(GhostVertex) };

    if (CreateUploadBuffer(device, ibSize, &m.ib) != GHOST_OK) return GHOST_FAIL;
    m.ib->Map(0, &rr, &p);
    memcpy(p, idx, ibSize);
    m.ib->Unmap(0, nullptr);
    m.ibView = { m.ib->GetGPUVirtualAddress(), ibSize, DXGI_FORMAT_R16_UINT };
    m.indexCount = ibSize / sizeof(unsigned short);
    return GHOST_OK;
}

// ============================================================
//  GhostPipelineCreate
// ============================================================

GhostResult GhostPipelineCreate(IGhostRHI* rhi,
    const GhostPipelineDesc* desc,
    GhostPipeline** outPipeline)
{
    GhostPipeline* p = new GhostPipeline();
    ID3D12Device* device = rhi->GetDevice();

    p->viewport = { 0, 0, (float)desc->width, (float)desc->height, 0.0f, 1.0f };
    p->scissorRect = { 0, 0, (long)desc->width,  (long)desc->height };

    // ---- Root Signature — b0 (per-object), b1 (scene), table t0 (texture), 4 static samplers ----
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rp[3] = {};

    // b0 — per-object CB (transform + material), VS and PS.
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].Descriptor.RegisterSpace = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;     // was D3D12_SHADER_VISIBILITY_VERTEX

    // b1 — scene CB (ambient + lights), pixel shader only
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[1].Descriptor.ShaderRegister = 1;
    rp[1].Descriptor.RegisterSpace = 0;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // t0 — the object's texture, pixel shader only
    rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[2].DescriptorTable.NumDescriptorRanges = 1;
    rp[2].DescriptorTable.pDescriptorRanges = &srvRange;
    rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s0 linear/wrap, s1 linear/clamp, s2 point/wrap, s3 point/clamp
    D3D12_STATIC_SAMPLER_DESC samplers[4] = {};
    const D3D12_FILTER filters[4] = {
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_FILTER_MIN_MAG_MIP_POINT,  D3D12_FILTER_MIN_MAG_MIP_POINT };
    const D3D12_TEXTURE_ADDRESS_MODE modes[4] = {
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP };
    for (int i = 0; i < 4; i++)
    {
        samplers[i].Filter = filters[i];
        samplers[i].AddressU = modes[i];
        samplers[i].AddressV = modes[i];
        samplers[i].AddressW = modes[i];
        samplers[i].MipLODBias = 0.0f;
        samplers[i].MaxAnisotropy = 1;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplers[i].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        samplers[i].MinLOD = 0.0f;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = (UINT)i;
        samplers[i].RegisterSpace = 0;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 3;
    rsd.pParameters = rp;
    rsd.NumStaticSamplers = 4;
    rsd.pStaticSamplers = samplers;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rsBlob, rsErr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1,
        &rsBlob, &rsErr)))
    {
        delete p; return GHOST_FAIL;
    }

    if (FAILED(device->CreateRootSignature(0, rsBlob->GetBufferPointer(),
        rsBlob->GetBufferSize(),
        IID_PPV_ARGS(&p->rootSignature))))
    {
        delete p; return GHOST_FAIL;
    }

    // ---- Shaders ----
    ComPtr<ID3DBlob> vs, ps;
    if (CompileShaderFromFile(L"shaders/GhostVS.hlsl", "VSMain", "vs_5_0", &vs) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    if (CompileShaderFromFile(L"shaders/GhostPS.hlsl", "PSMain", "ps_5_0", &ps) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    // ---- PSO ----
    D3D12_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
 
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = p->rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { layout, 4 }; 
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.NumRenderTargets = 1;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&p->pso))))
    {
        delete p; return GHOST_FAIL;
    }

    // ---- Icon PSO: unlit billboard quads, no vertex buffer, drawn on top ----
    ComPtr<ID3DBlob> ivs, ips;
    if (CompileShaderFromFile(L"shaders/GhostVS.hlsl", "IconVS", "vs_5_0", &ivs) != GHOST_OK ||
        CompileShaderFromFile(L"shaders/GhostPS.hlsl", "IconPS", "ps_5_0", &ips) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC iconDesc = psoDesc;
    iconDesc.VS = { ivs->GetBufferPointer(), ivs->GetBufferSize() };
    iconDesc.PS = { ips->GetBufferPointer(), ips->GetBufferSize() };
    iconDesc.InputLayout = { nullptr, 0 };
    iconDesc.DepthStencilState.DepthEnable = FALSE;
    iconDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    if (FAILED(device->CreateGraphicsPipelineState(&iconDesc, IID_PPV_ARGS(&p->iconPso))))
    {
        delete p; return GHOST_FAIL;
    }
     // ---- Constant Buffer ----
    const unsigned long long cbSizeTotal = GHOST_OBJECT_CB_STRIDE * GHOST_MAX_OBJECTS;

    if (CreateUploadBuffer(device, cbSizeTotal, &p->constantBuffer) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_RANGE rr = { 0, 0 };
    p->constantBuffer->Map(0, &rr, &p->cbvMappedData);

    if (CreateUploadBuffer(device, GHOST_SCENE_CB_SIZE, &p->sceneBuffer) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    p->sceneBuffer->Map(0, &rr, &p->sceneMappedData);
    memset(p->sceneMappedData, 0, (size_t)GHOST_SCENE_CB_SIZE);

    // ---- CBV Heap ----
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = GHOST_MAX_TEXTURES;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&p->cbvHeap));
    p->srvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // ---- DSV Heap + Depth Buffer ----
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&p->dsvHeap));

    CreateDepthBuffer(device, desc->width, desc->height, p);

    *outPipeline = p;
    return GHOST_OK;
}

void GhostUpdateSceneCB(GhostPipeline* p, const GhostSceneCB* cb)
{
    memcpy(p->sceneMappedData, cb, sizeof(GhostSceneCB));
}

int GhostCreateTexture(IGhostRHI* rhi, GhostPipeline* p,
    const unsigned char* rgba, unsigned int width, unsigned int height, int replaceIndex)
{
    if (!rgba || width == 0 || height == 0) return -1;
    if (replaceIndex < 0 && p->textureCount >= GHOST_MAX_TEXTURES) return -1;
    if (replaceIndex >= (int)p->textureCount) return -1;

    ID3D12Device* device = rhi->GetDevice();

    std::vector<MipLevel> mips;
    BuildMipChain(rgba, width, height, mips);
    const UINT mipCount = (UINT)mips.size();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = width;
    td.Height = height;
    td.DepthOrArraySize = 1;
    td.MipLevels = (UINT16)mipCount;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;      // hardware decodes sRGB -> linear on sample
    td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    ComPtr<ID3D12Resource> tex;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) return -1;

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(mipCount);
    std::vector<UINT>   numRows(mipCount);
    std::vector<UINT64> rowBytes(mipCount);
    UINT64 total = 0;
    device->GetCopyableFootprints(&td, 0, mipCount, 0, fp.data(), numRows.data(), rowBytes.data(), &total);

    ComPtr<ID3D12Resource> upload;
    if (CreateUploadBuffer(device, total, &upload) != GHOST_OK) return -1;

    unsigned char* dst = nullptr;
    D3D12_RANGE rr = { 0, 0 };
    upload->Map(0, &rr, (void**)&dst);
    for (UINT i = 0; i < mipCount; i++)
        for (UINT row = 0; row < numRows[i]; row++)
            memcpy(dst + fp[i].Offset + (UINT64)row * fp[i].Footprint.RowPitch,
                mips[i].px.data() + (size_t)row * mips[i].w * 4,
                (size_t)mips[i].w * 4);
    upload->Unmap(0, nullptr);

    ID3D12Resource* texPtr = tex.Get();
    ID3D12Resource* upPtr = upload.Get();
    GhostResult r = rhi->ExecuteOneShot([&](ID3D12GraphicsCommandList* cmd)
        {
            for (UINT i = 0; i < mipCount; i++)
            {
                D3D12_TEXTURE_COPY_LOCATION dl = {};
                dl.pResource = texPtr;
                dl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dl.SubresourceIndex = i;

                D3D12_TEXTURE_COPY_LOCATION sl = {};
                sl.pResource = upPtr;
                sl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                sl.PlacedFootprint = fp[i];

                cmd->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr);
            }

            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = texPtr;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            cmd->ResourceBarrier(1, &b);
        });
    if (r != GHOST_OK) return -1;            // GPU is idle again here, so the upload buffer can go

    const unsigned int idx = (replaceIndex >= 0) ? (unsigned int)replaceIndex : p->textureCount++;
    p->textures[idx] = tex;       // when replacing, the old resource is released here; the GPU is idle

    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = td.Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = mipCount;

    D3D12_CPU_DESCRIPTOR_HANDLE h = p->cbvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)idx * p->srvIncrement;
    device->CreateShaderResourceView(tex.Get(), &sd, h);

    return (int)idx;
}


void GhostUpdateObjectCB(GhostPipeline* p, unsigned int slot, const GhostObjectCB* cb)
{
    if (slot >= GHOST_MAX_OBJECTS) return;
    memcpy((char*)p->cbvMappedData + slot * GHOST_OBJECT_CB_STRIDE, cb, sizeof(GhostObjectCB));
}

GhostResult GhostUploadMeshes(IGhostRHI* rhi, GhostPipeline* p)
{
    ID3D12Device* d = rhi->GetDevice();
    if (UploadMesh(d, p->meshes[GHOST_MESH_CUBE],
        CubeVertices, sizeof(CubeVertices), CubeIndices, sizeof(CubeIndices)) != GHOST_OK) return GHOST_FAIL;
    if (UploadMesh(d, p->meshes[GHOST_MESH_TRIANGLE],
        TriangleVertices, sizeof(TriangleVertices), TriangleIndices, sizeof(TriangleIndices)) != GHOST_OK) return GHOST_FAIL;
    if (UploadMesh(d, p->meshes[GHOST_MESH_SHIP],
        ShipVertices, sizeof(ShipVertices), ShipIndices, sizeof(ShipIndices)) != GHOST_OK) return GHOST_FAIL;
    return GHOST_OK;
}
void GhostDrawIcon(IGhostRHI* rhi, GhostPipeline* p, unsigned int slot)
{
    if (slot >= GHOST_MAX_OBJECTS) return;
    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->iconPso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, p->constantBuffer->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2, p->cbvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);       // one quad, corners generated from SV_VertexID
}

void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* p, int meshId, unsigned int slot,
    unsigned int textureIndex, unsigned int firstIndex, unsigned int indexCount)
{
    if (meshId < 0 || meshId >= GHOST_MESH_COUNT || slot >= GHOST_MAX_OBJECTS) return;
    const GhostMesh& m = p->meshes[meshId];
    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->pso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    cmd->SetGraphicsRootConstantBufferView(
        0, p->constantBuffer->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());
    if (textureIndex >= p->textureCount) textureIndex = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)textureIndex * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m.vbView);
    cmd->IASetIndexBuffer(&m.ibView);
    cmd->DrawIndexedInstanced(indexCount ? indexCount : m.indexCount, 1, firstIndex, 0, 0);
}


// ============================================================
//  Resize + Destroy
// ============================================================

GhostResult GhostResizeDepthBuffer(IGhostRHI* rhi, GhostPipeline* pipeline,
    unsigned int width, unsigned int height)
{
    return CreateDepthBuffer(rhi->GetDevice(), width, height, pipeline);
}

void GhostPipelineDestroy(GhostPipeline* pipeline)
{
    if (!pipeline) return;
    if (pipeline->cbvMappedData)
        pipeline->constantBuffer->Unmap(0, nullptr);
    if (pipeline->sceneMappedData)
        pipeline->sceneBuffer->Unmap(0, nullptr);
    delete pipeline;
}