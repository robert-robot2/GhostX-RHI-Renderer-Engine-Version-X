// GhostPipeline.cpp
#include "../include/GhostPipeline.h"
#include "../include/GhostMath.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <string>
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
static GhostVertex QuadVertices[] =
{
    { -0.5f,  0.5f, 0.0f,  1,1,1,1,  0,0,-1,  0,0 },
    { 0.5f,  0.5f, 0.0f,  1,1,1,1,  0,0,-1,  1,0 },
    { 0.5f, -0.5f, 0.0f,  1,1,1,1,  0,0,-1,  1,1 },
    { -0.5f, -0.5f, 0.0f,  1,1,1,1,  0,0,-1,  0,1 },
};
static unsigned short QuadIndices[] = { 0, 1, 2,  0, 2, 3 };

bool GhostGetMeshCPU(GhostPipeline* p, int meshId, GhostMeshCPU* out)
{
    *out = GhostMeshCPU();
    switch (meshId)
    {
    case GHOST_MESH_CUBE:
    case GHOST_MESH_SQUARE:
        out->verts = CubeVertices;  out->vertexCount = sizeof(CubeVertices) / sizeof(CubeVertices[0]);
        out->idx16 = CubeIndices;   out->indexCount = sizeof(CubeIndices) / sizeof(CubeIndices[0]);
        return true;
    case GHOST_MESH_TRIANGLE:
        out->verts = TriangleVertices;  out->vertexCount = sizeof(TriangleVertices) / sizeof(TriangleVertices[0]);
        out->idx16 = TriangleIndices;   out->indexCount = sizeof(TriangleIndices) / sizeof(TriangleIndices[0]);
        return true;
    case GHOST_MESH_SHIP:
        out->verts = ShipVertices;  out->vertexCount = sizeof(ShipVertices) / sizeof(ShipVertices[0]);
        out->idx16 = ShipIndices;   out->indexCount = sizeof(ShipIndices) / sizeof(ShipIndices[0]);
        return true;
    case GHOST_MESH_QUAD:
        out->verts = QuadVertices;  out->vertexCount = sizeof(QuadVertices) / sizeof(QuadVertices[0]);
        out->idx16 = QuadIndices;   out->indexCount = sizeof(QuadIndices) / sizeof(QuadIndices[0]);
        return true;
    }
    if (p && meshId >= GHOST_MESH_COUNT && meshId < GHOST_MAX_MESHES)
    {
        const GhostMesh& m = p->meshes[meshId];
        if (m.cpuVerts.empty() || m.cpuIdx.empty()) return false;
        out->verts = m.cpuVerts.data();  out->vertexCount = (unsigned int)m.cpuVerts.size();
        out->idx32 = m.cpuIdx.data();    out->indexCount = (unsigned int)m.cpuIdx.size();
        return true;
    }
    return false;
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

GhostResult GhostUploadMeshData(IGhostRHI* rhi, GhostPipeline* p, int meshId,
    const void* verts, unsigned int vertexCount, const unsigned int* indices, unsigned int indexCount)
{
    if (meshId < GHOST_MESH_COUNT || meshId >= GHOST_MAX_MESHES || !verts || !indices ||
        vertexCount == 0 || indexCount < 3) return GHOST_FAIL;

    const unsigned long long vbSize = (unsigned long long)vertexCount * sizeof(GhostVertex);
    const unsigned long long ibSize = (unsigned long long)indexCount * sizeof(unsigned int);
    if (vbSize > 0xFFFFFFFFull || ibSize > 0xFFFFFFFFull) return GHOST_FAIL;     // buffer views use 32-bit sizes

    ID3D12Device* d = rhi->GetDevice();
    GhostMesh m;
    D3D12_RANGE rr = { 0, 0 };
    void* ptr = nullptr;

    if (CreateUploadBuffer(d, vbSize, &m.vb) != GHOST_OK) return GHOST_FAIL;
    m.vb->Map(0, &rr, &ptr);
    memcpy(ptr, verts, (size_t)vbSize);
    m.vb->Unmap(0, nullptr);
    m.vbView = { m.vb->GetGPUVirtualAddress(), (UINT)vbSize, sizeof(GhostVertex) };

    if (CreateUploadBuffer(d, ibSize, &m.ib) != GHOST_OK) return GHOST_FAIL;
    m.ib->Map(0, &rr, &ptr);
    memcpy(ptr, indices, (size_t)ibSize);
    m.ib->Unmap(0, nullptr);
    m.ibView = { m.ib->GetGPUVirtualAddress(), (UINT)ibSize, DXGI_FORMAT_R32_UINT };
    m.indexCount = indexCount;

    m.cpuVerts.resize(vertexCount);
    memcpy(m.cpuVerts.data(), verts, (size_t)vbSize);
    m.cpuIdx.assign(indices, indices + indexCount);

    p->meshes[meshId] = std::move(m);
    return GHOST_OK;
}

// ============================================================
//  Internal helpers  (no RHI dependency — take raw device)
// ============================================================

static GhostResult CreateDepthBuffer(ID3D12Device* device,
    unsigned int width, unsigned int height,
    GhostPipeline* p, int surfaceId)
{
    if (surfaceId < 0 || surfaceId >= GHOST_MAX_SURFACES) return GHOST_FAIL;
    p->depthBuffers[surfaceId].Reset();
    p->depthW[surfaceId] = p->depthH[surfaceId] = 0;

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
        IID_PPV_ARGS(&p->depthBuffers[surfaceId]));
    if (FAILED(hr)) return GHOST_FAIL;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    D3D12_CPU_DESCRIPTOR_HANDLE h = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)surfaceId * p->dsvIncrement;
    device->CreateDepthStencilView(p->depthBuffers[surfaceId].Get(), &dsvDesc, h);
    p->depthW[surfaceId] = width;
    p->depthH[surfaceId] = height;
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


// Folder that GhostX.dll lives in (with the trailing backslash)
static std::wstring ModuleDir()
{
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&ModuleDir, &h);
    wchar_t buf[1024];
    DWORD n = GetModuleFileNameW(h, buf, 1024);
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash + 1);
}

// shaders\<file> next to the DLL, falling back to the old working-directory path
static std::wstring ShaderPath(const wchar_t* file)
{
    std::wstring a = ModuleDir() + L"shaders\\" + file;
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
    return std::wstring(L"shaders/") + file;
}
// Every draw that uses the main root signature binds the shadow maps (parameter 3)
static void BindShadowTable(ID3D12GraphicsCommandList* cmd, GhostPipeline* p)
{
    D3D12_GPU_DESCRIPTOR_HANDLE h = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)GHOST_SHADOW_SRV_INDEX * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(3, h);
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

struct UIVertex { float x, y, u, v, r, g, b, a; };      // 32 bytes

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

    // add after SRV range block this may be incorrrect
    D3D12_DESCRIPTOR_RANGE shadowRange = {};
    shadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    shadowRange.NumDescriptors = 1;
    shadowRange.BaseShaderRegister = 1;                       // t1
    shadowRange.RegisterSpace = 0;
    shadowRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rp[4] = {};

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

    // t1 — the shadow maps (texture array), pixel shader only
    rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp[3].DescriptorTable.NumDescriptorRanges = 1;
    rp[3].DescriptorTable.pDescriptorRanges = &shadowRange;
    rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

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
    rsd.NumParameters = 4;
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
    // ---- Shadow root signature: b0 object, b2 root constants (light matrix etc.), t0 texture (sprite alpha) ----
    D3D12_ROOT_PARAMETER sp[3] = {};
    sp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    sp[0].Descriptor.ShaderRegister = 0;
    sp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    sp[1].Constants.ShaderRegister = 2;
    sp[1].Constants.RegisterSpace = 0;
    sp[1].Constants.Num32BitValues = 24;
    sp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    sp[2].DescriptorTable.NumDescriptorRanges = 1;
    sp[2].DescriptorTable.pDescriptorRanges = &srvRange;
    sp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC ssd = {};
    ssd.NumParameters = 3;
    ssd.pParameters = sp;
    ssd.NumStaticSamplers = 4;
    ssd.pStaticSamplers = samplers;
    ssd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> ssBlob, ssErr;
    if (FAILED(D3D12SerializeRootSignature(&ssd, D3D_ROOT_SIGNATURE_VERSION_1, &ssBlob, &ssErr)) ||
        FAILED(device->CreateRootSignature(0, ssBlob->GetBufferPointer(), ssBlob->GetBufferSize(),
            IID_PPV_ARGS(&p->shadowRootSig))))
    {
        delete p; return GHOST_FAIL;
    }
    // ---- Shaders ----
    ComPtr<ID3DBlob> vs, ps;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "VSMain", "vs_5_0", &vs) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    if (CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "PSMain", "ps_5_0", &ps) != GHOST_OK)
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
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "IconVS", "vs_5_0", &ivs) != GHOST_OK ||
        CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "IconPS", "ps_5_0", &ips) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    // ---- Blit PSO: fullscreen quad that shows an offscreen image, no depth ----
    ComPtr<ID3DBlob> bvs, bps;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "BlitVS", "vs_5_0", &bvs) != GHOST_OK ||
        CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "BlitPS", "ps_5_0", &bps) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }



    D3D12_GRAPHICS_PIPELINE_STATE_DESC blitDesc = psoDesc;
    blitDesc.VS = { bvs->GetBufferPointer(), bvs->GetBufferSize() };
    blitDesc.PS = { bps->GetBufferPointer(), bps->GetBufferSize() };
    blitDesc.InputLayout = { nullptr, 0 };
    blitDesc.DepthStencilState.DepthEnable = FALSE;
    blitDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    blitDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    if (FAILED(device->CreateGraphicsPipelineState(&blitDesc, IID_PPV_ARGS(&p->blitPso))))
    {
        delete p; return GHOST_FAIL;
    }

    // ---- UI PSO: alpha-blended quads, no depth ----
    ComPtr<ID3DBlob> uvs, ups;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "UIVS", "vs_5_0", &uvs) != GHOST_OK ||
        CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "UIPS", "ps_5_0", &ups) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_INPUT_ELEMENT_DESC uiLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0,  8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC uiDesc = psoDesc;
    uiDesc.VS = { uvs->GetBufferPointer(), uvs->GetBufferSize() };
    uiDesc.PS = { ups->GetBufferPointer(), ups->GetBufferSize() };
    uiDesc.InputLayout = { uiLayout, 3 };
    uiDesc.DepthStencilState.DepthEnable = FALSE;
    uiDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    D3D12_RENDER_TARGET_BLEND_DESC& bl = uiDesc.BlendState.RenderTarget[0];
    bl.BlendEnable = TRUE;
    bl.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    bl.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOp = D3D12_BLEND_OP_ADD;
    bl.SrcBlendAlpha = D3D12_BLEND_ONE;
    bl.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    bl.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    bl.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(device->CreateGraphicsPipelineState(&uiDesc, IID_PPV_ARGS(&p->uiPso))))
    {
        delete p; return GHOST_FAIL;
    }

    if (CreateUploadBuffer(device, sizeof(UIVertex) * 6 * GHOST_UI_MAX_QUADS, &p->uiVB) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    D3D12_RANGE uiRange = { 0, 0 };
    p->uiVB->Map(0, &uiRange, &p->uiMapped);

    // ---- Overlay PSOs: float3 position, so grid lines can be depth tested ----
    ComPtr<ID3DBlob> ovs;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "OverlayVS", "vs_5_0", &ovs) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    D3D12_INPUT_ELEMENT_DESC ovLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC ovTop = uiDesc;               // alpha blended, no depth, pixel shader UIPS
    ovTop.VS = { ovs->GetBufferPointer(), ovs->GetBufferSize() };
    ovTop.InputLayout = { ovLayout, 3 };
    if (FAILED(device->CreateGraphicsPipelineState(&ovTop, IID_PPV_ARGS(&p->overlayTopPso))))
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC ovDepth = ovTop;
    ovDepth.DepthStencilState.DepthEnable = TRUE;
    ovDepth.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ovDepth.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&ovDepth, IID_PPV_ARGS(&p->overlayDepthPso))))
    {
        delete p; return GHOST_FAIL;
    }

    if (CreateUploadBuffer(device, sizeof(GhostOverlayVertex) * GHOST_OVERLAY_MAX_VERTS, &p->ovVB) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }
    D3D12_RANGE ovRange = { 0, 0 };
    p->ovVB->Map(0, &ovRange, &p->ovMapped);


    // ---- Sky PSO: full-screen triangle, alpha blended, no depth ----
    ComPtr<ID3DBlob> svs, sps;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "SkyVS", "vs_5_0", &svs) != GHOST_OK ||
        CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "SkyPS", "ps_5_0", &sps) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyDesc = psoDesc;
    skyDesc.VS = { svs->GetBufferPointer(), svs->GetBufferSize() };
    skyDesc.PS = { sps->GetBufferPointer(), sps->GetBufferSize() };
    skyDesc.InputLayout = { nullptr, 0 };
    skyDesc.DepthStencilState.DepthEnable = FALSE;
    skyDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    D3D12_RENDER_TARGET_BLEND_DESC& sb = skyDesc.BlendState.RenderTarget[0];
    sb.BlendEnable = TRUE;
    sb.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    sb.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    sb.BlendOp = D3D12_BLEND_OP_ADD;
    sb.SrcBlendAlpha = D3D12_BLEND_ONE;
    sb.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    sb.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    sb.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(device->CreateGraphicsPipelineState(&skyDesc, IID_PPV_ARGS(&p->skyPso))))
    {
        delete p; return GHOST_FAIL;
    }

    // ---- Shadow PSO: depth only, double sided, the pixel shader writes the depth ----
    ComPtr<ID3DBlob> shv, shp;
    if (CompileShaderFromFile(ShaderPath(L"GhostVS.hlsl").c_str(), "ShadowVS", "vs_5_0", &shv) != GHOST_OK ||
        CompileShaderFromFile(ShaderPath(L"GhostPS.hlsl").c_str(), "ShadowPS", "ps_5_0", &shp) != GHOST_OK)
    {
        delete p; return GHOST_FAIL;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shd = {};
    shd.pRootSignature = p->shadowRootSig.Get();
    shd.VS = { shv->GetBufferPointer(), shv->GetBufferSize() };
    shd.PS = { shp->GetBufferPointer(), shp->GetBufferSize() };
    shd.InputLayout = { layout, 4 };
    shd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shd.NumRenderTargets = 0;
    shd.SampleDesc.Count = 1;
    shd.SampleMask = UINT_MAX;
    shd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    shd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shd.RasterizerState.DepthClipEnable = FALSE;            // casters in front of the light's near plane still count
    shd.DepthStencilState.DepthEnable = TRUE;
    shd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shd.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    if (FAILED(device->CreateGraphicsPipelineState(&shd, IID_PPV_ARGS(&p->shadowPso))))
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
      hd.NumDescriptors = GHOST_MAX_TEXTURES + GHOST_MAX_SURFACES + 1;     // textures, offscreen targets, then the shadow maps
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&p->cbvHeap));
    p->srvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    // ---- DSV Heap + Depth Buffer ----
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = GHOST_MAX_SURFACES;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&p->dsvHeap));
    p->dsvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    // RTV Descriptor
    D3D12_DESCRIPTOR_HEAP_DESC offRtvDesc = {};
    offRtvDesc.NumDescriptors = GHOST_MAX_SURFACES;
    offRtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    offRtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    device->CreateDescriptorHeap(&offRtvDesc, IID_PPV_ARGS(&p->offRtvHeap));
    p->rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CreateDepthBuffer(device, desc->width, desc->height, p, 0);
    // ---- Shadow maps: one array texture, a DSV per slice, one SRV over the whole array ----
    {
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = GHOST_SHADOW_SIZE;
        rd.Height = GHOST_SHADOW_SIZE;
        rd.DepthOrArraySize = GHOST_SHADOW_SLICES;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R32_TYPELESS;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE cv = {};
        cv.Format = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 1.0f;

        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&p->shadowMap))))
        {
            delete p; return GHOST_FAIL;
        }

        D3D12_DESCRIPTOR_HEAP_DESC sdh = {};
        sdh.NumDescriptors = GHOST_SHADOW_SLICES;
        sdh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device->CreateDescriptorHeap(&sdh, IID_PPV_ARGS(&p->shadowDsvHeap))))
        {
            delete p; return GHOST_FAIL;
        }
        p->shadowDsvInc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        for (UINT i = 0; i < GHOST_SHADOW_SLICES; i++)
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
            dv.Format = DXGI_FORMAT_D32_FLOAT;
            dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            dv.Texture2DArray.MipSlice = 0;
            dv.Texture2DArray.FirstArraySlice = i;
            dv.Texture2DArray.ArraySize = 1;
            D3D12_CPU_DESCRIPTOR_HANDLE h = p->shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += (SIZE_T)i * p->shadowDsvInc;
            device->CreateDepthStencilView(p->shadowMap.Get(), &dv, h);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R32_FLOAT;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2DArray.MostDetailedMip = 0;
        sv.Texture2DArray.MipLevels = 1;
        sv.Texture2DArray.FirstArraySlice = 0;
        sv.Texture2DArray.ArraySize = GHOST_SHADOW_SLICES;
        D3D12_CPU_DESCRIPTOR_HANDLE sh = p->cbvHeap->GetCPUDescriptorHandleForHeapStart();
        sh.ptr += (SIZE_T)GHOST_SHADOW_SRV_INDEX * p->srvIncrement;
        device->CreateShaderResourceView(p->shadowMap.Get(), &sv, sh);

        if (CreateUploadBuffer(device, GHOST_OBJECT_CB_STRIDE * GHOST_MAX_SHADOW_CASTERS, &p->shadowCB) != GHOST_OK)
        {
            delete p; return GHOST_FAIL;
        }
        D3D12_RANGE srr = { 0, 0 };
        p->shadowCB->Map(0, &srr, &p->shadowCBMapped);
    }
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
    if (UploadMesh(d, p->meshes[GHOST_MESH_SQUARE],
        CubeVertices, sizeof(CubeVertices), CubeIndices, sizeof(CubeIndices)) != GHOST_OK) return GHOST_FAIL;
    if (UploadMesh(d, p->meshes[GHOST_MESH_QUAD],
        QuadVertices, sizeof(QuadVertices), QuadIndices, sizeof(QuadIndices)) != GHOST_OK) return GHOST_FAIL;
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
    BindShadowTable(cmd, p);
    cmd->SetGraphicsRootConstantBufferView(
        0, p->constantBuffer->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootDescriptorTable(2, p->cbvHeap->GetGPUDescriptorHandleForHeapStart());
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvOverrideActive ? p->rtvOverride : rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, rhi->GetCurrentSurface());
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);       // one quad, corners generated from SV_VertexID
}

void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* p, int meshId, unsigned int slot,
    unsigned int textureIndex, unsigned int firstIndex, unsigned int indexCount)
{

    // double check this for 11.a

    if (meshId < 0 || meshId >= GHOST_MAX_MESHES || slot >= GHOST_MAX_OBJECTS) return;
    const GhostMesh& m = p->meshes[meshId];
    if (!m.vb) return;




    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };


    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->pso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    BindShadowTable(cmd, p);


    cmd->SetGraphicsRootConstantBufferView(
        0, p->constantBuffer->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());
    if (textureIndex >= p->textureCount) textureIndex = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)textureIndex * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvOverrideActive ? p->rtvOverride : rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, rhi->GetCurrentSurface());
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m.vbView);
    cmd->IASetIndexBuffer(&m.ibView);
    cmd->DrawIndexedInstanced(indexCount ? indexCount : m.indexCount, 1, firstIndex, 0, 0);
}


// ============================================================
//  Resize + Destroy
// ============================================================

GhostResult GhostResizeDepthBuffer(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId,
    unsigned int width, unsigned int height)
{
    return CreateDepthBuffer(rhi->GetDevice(), width, height, pipeline, surfaceId);
}

D3D12_CPU_DESCRIPTOR_HANDLE GhostGetDSV(GhostPipeline* p, int surfaceId)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)surfaceId * p->dsvIncrement;
    return h;
}

void GhostReleaseDepth(GhostPipeline* p, int surfaceId)
{
    if (surfaceId >= 0 && surfaceId < GHOST_MAX_SURFACES)
    {
        p->depthBuffers[surfaceId].Reset();
        p->depthW[surfaceId] = p->depthH[surfaceId] = 0;
    }
}

void GhostPipelineDestroy(GhostPipeline* pipeline)
{
    if (!pipeline) return;
    if (pipeline->cbvMappedData)
        pipeline->constantBuffer->Unmap(0, nullptr);
    if (pipeline->sceneMappedData)
        pipeline->sceneBuffer->Unmap(0, nullptr);
    if (pipeline->uiMapped)
        pipeline->uiVB->Unmap(0, nullptr);
    if (pipeline->shadowCBMapped)
        pipeline->shadowCB->Unmap(0, nullptr);
    if (pipeline->ovMapped)
        pipeline->ovVB->Unmap(0, nullptr);
    delete pipeline;
}

// ============================================================
//  Render resolution: offscreen targets + blit
//  (every frame ends with a GPU flush, so resources can be replaced between frames)
// ============================================================
GhostResult GhostEnsureDepth(IGhostRHI* rhi, GhostPipeline* p, int id, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (p->depthBuffers[id] && p->depthW[id] == w && p->depthH[id] == h) return GHOST_OK;
    return CreateDepthBuffer(rhi->GetDevice(), w, h, p, id);
}

GhostResult GhostEnsureOffscreen(IGhostRHI* rhi, GhostPipeline* p, int id, unsigned int w, unsigned int h)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || w == 0 || h == 0) return GHOST_FAIL;
    if (p->offTargets[id] && p->offW[id] == w && p->offH[id] == h) return GHOST_OK;

    ID3D12Device* device = rhi->GetDevice();
    p->offTargets[id].Reset();
    p->offW[id] = p->offH[id] = 0;

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

    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&p->offTargets[id]))))
        return GHOST_FAIL;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GhostGetOffscreenRTV(p, id);
    device->CreateRenderTargetView(p->offTargets[id].Get(), nullptr, rtv);

    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = rd.Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetCPUDescriptorHandleForHeapStart();
    srv.ptr += (SIZE_T)(GHOST_MAX_TEXTURES + id) * p->srvIncrement;
    device->CreateShaderResourceView(p->offTargets[id].Get(), &sd, srv);

    p->offW[id] = w;
    p->offH[id] = h;
    return GHOST_OK;
}

void GhostReleaseOffscreen(GhostPipeline* p, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    p->offTargets[id].Reset();
    p->offW[id] = p->offH[id] = 0;
}

D3D12_CPU_DESCRIPTOR_HANDLE GhostGetOffscreenRTV(GhostPipeline* p, int id)
{
    D3D12_CPU_DESCRIPTOR_HANDLE h = p->offRtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)id * p->rtvIncrement;
    return h;
}

void GhostBeginOffscreen(IGhostRHI* rhi, GhostPipeline* p, int id,
    const GhostColor* clear, unsigned int w, unsigned int h)
{
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = p->offTargets[id].Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmd->ResourceBarrier(1, &b);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GhostGetOffscreenRTV(p, id);
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, id);
    float c[4] = { clear->r, clear->g, clear->b, clear->a };
    cmd->ClearRenderTargetView(rtv, c, 0, nullptr);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    p->rtvOverride = rtv;
    p->rtvOverrideActive = true;
}

void GhostEndOffscreen(IGhostRHI* rhi, GhostPipeline* p, int id)
{
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = p->offTargets[id].Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &b);

    p->rtvOverrideActive = false;
}

// Draws the offscreen image into the surface's back buffer, inside the rect (x, y, w, h)
void GhostDrawBlit(IGhostRHI* rhi, GhostPipeline* p, int id, float x, float y, float w, float h)
{
    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->blitPso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    BindShadowTable(cmd, p);
    cmd->SetGraphicsRootConstantBufferView(0, p->constantBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)(GHOST_MAX_TEXTURES + id) * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rhi->GetRTV();            // the window's back buffer
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { x, y, w, h, 0.0f, 1.0f };
    D3D12_RECT sc = { (LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + h) };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);
}

void GhostDrawUI(IGhostRHI* rhi, GhostPipeline* p, const std::vector<GhostUIQuad>& quads,
    unsigned int w, unsigned int h, float scale)
{
    if (quads.empty() || !p->uiPso || !p->uiMapped || w == 0 || h == 0) return;
    const size_t n = quads.size() < GHOST_UI_MAX_QUADS ? quads.size() : GHOST_UI_MAX_QUADS;

    struct Run { int tex; UINT first, count; };
    std::vector<Run> runs;

    UIVertex* v = (UIVertex*)p->uiMapped;
    const float sx = 2.0f * scale / (float)w, sy = 2.0f * scale / (float)h;

    for (size_t i = 0; i < n; i++)
    {
        const GhostUIQuad& q = quads[i];
        const float x0 = q.x * sx - 1.0f, x1 = (q.x + q.w) * sx - 1.0f;
        const float y0 = 1.0f - q.y * sy, y1 = 1.0f - (q.y + q.h) * sy;

        const UIVertex tl = { x0, y0, q.u0, q.v0, q.color[0], q.color[1], q.color[2], q.color[3] };
        const UIVertex tr = { x1, y0, q.u1, q.v0, q.color[0], q.color[1], q.color[2], q.color[3] };
        const UIVertex br = { x1, y1, q.u1, q.v1, q.color[0], q.color[1], q.color[2], q.color[3] };
        const UIVertex bl = { x0, y1, q.u0, q.v1, q.color[0], q.color[1], q.color[2], q.color[3] };

        UIVertex* o = v + i * 6;
        o[0] = tl; o[1] = tr; o[2] = br;
        o[3] = tl; o[4] = br; o[5] = bl;

        if (!runs.empty() && runs.back().tex == q.textureId) runs.back().count += 6;
        else runs.push_back({ q.textureId, (UINT)(i * 6), 6 });
    }

    auto* cmd = rhi->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->uiPso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    BindShadowTable(cmd, p);
    cmd->SetGraphicsRootConstantBufferView(0, p->constantBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvOverrideActive ? p->rtvOverride : rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, rhi->GetCurrentSurface());
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VERTEX_BUFFER_VIEW vbv = { p->uiVB->GetGPUVirtualAddress(), (UINT)(n * 6 * sizeof(UIVertex)), sizeof(UIVertex) };
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv);

    for (const Run& r : runs)
    {
        unsigned int ti = (r.tex >= 0 && (unsigned int)r.tex < p->textureCount) ? (unsigned int)r.tex : 0;
        D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
        srv.ptr += (UINT64)ti * p->srvIncrement;
        cmd->SetGraphicsRootDescriptorTable(2, srv);
        cmd->DrawInstanced(r.count, 1, r.first, 0);
    }
}

void GhostDrawSky(IGhostRHI* rhi, GhostPipeline* p, unsigned int slot, unsigned int textureIndex)
{
    if (slot >= GHOST_MAX_OBJECTS || !p->skyPso) return;
    if (textureIndex >= p->textureCount) textureIndex = 0;
    auto* cmd = rhi->GetCmdList();

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };

    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->skyPso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    BindShadowTable(cmd, p);

    cmd->SetGraphicsRootConstantBufferView(
        0, p->constantBuffer->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)textureIndex * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvOverrideActive ? p->rtvOverride : rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, rhi->GetCurrentSurface());
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
}


// ============================================================
//  Shadow pass
// ============================================================
void GhostUpdateShadowCB(GhostPipeline* p, unsigned int slot, const GhostObjectCB* cb)
{
    if (slot >= GHOST_MAX_SHADOW_CASTERS || !p->shadowCBMapped) return;
    memcpy((char*)p->shadowCBMapped + slot * GHOST_OBJECT_CB_STRIDE, cb, sizeof(GhostObjectCB));
}

void GhostShadowBegin(IGhostRHI* rhi, GhostPipeline* p)
{
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = p->shadowMap.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    cmd->ResourceBarrier(1, &b);

    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(p->shadowPso.Get());
    cmd->SetGraphicsRootSignature(p->shadowRootSig.Get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void GhostShadowSlice(IGhostRHI* rhi, GhostPipeline* p, int slice, const float* vp16, const float* posFar4, float mode)
{
    auto* cmd = rhi->GetCmdList();

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
    dsv.ptr += (SIZE_T)slice * p->shadowDsvInc;
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)GHOST_SHADOW_SIZE, (float)GHOST_SHADOW_SIZE, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)GHOST_SHADOW_SIZE, (LONG)GHOST_SHADOW_SIZE };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);

    float k[24] = {};
    memcpy(k, vp16, 16 * sizeof(float));
    memcpy(k + 16, posFar4, 4 * sizeof(float));
    k[20] = mode;
    cmd->SetGraphicsRoot32BitConstants(1, 24, k, 0);
}

void GhostShadowDraw(IGhostRHI* rhi, GhostPipeline* p, int meshId, unsigned int slot, unsigned int texIndex)
{
    if (meshId < 0 || meshId >= GHOST_MAX_MESHES || slot >= GHOST_MAX_SHADOW_CASTERS) return;
    const GhostMesh& m = p->meshes[meshId];
    if (!m.vb) return;

    auto* cmd = rhi->GetCmdList();

    cmd->SetGraphicsRootConstantBufferView(0, p->shadowCB->GetGPUVirtualAddress() + slot * GHOST_OBJECT_CB_STRIDE);

    if (texIndex >= p->textureCount) texIndex = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)texIndex * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);

    cmd->IASetVertexBuffers(0, 1, &m.vbView);
    cmd->IASetIndexBuffer(&m.ibView);
    cmd->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
}

// Back to a readable texture, and restore the viewport the main pass expects
void GhostShadowEnd(IGhostRHI* rhi, GhostPipeline* p, unsigned int w, unsigned int h)
{
    auto* cmd = rhi->GetCmdList();

    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = p->shadowMap.Get();
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &b);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    D3D12_RECT sc = { 0, 0, (LONG)w, (LONG)h };
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
}


void GhostOverlayBegin(GhostPipeline* p) { p->ovCursor = 0; }

void GhostDrawOverlay(IGhostRHI* rhi, GhostPipeline* p, const std::vector<GhostOverlayVertex>& v, int mode, int textureId)
{
    if (v.empty() || !p->ovMapped || !p->overlayTopPso) return;
    size_t n = v.size() - v.size() % 3;
    if ((size_t)p->ovCursor + n > GHOST_OVERLAY_MAX_VERTS)
    {
        n = GHOST_OVERLAY_MAX_VERTS - p->ovCursor;
        n -= n % 3;
    }
    if (n == 0) return;

    GhostOverlayVertex* dst = (GhostOverlayVertex*)p->ovMapped + p->ovCursor;
    memcpy(dst, v.data(), n * sizeof(GhostOverlayVertex));

    auto* cmd = rhi->GetCmdList();
    ID3D12DescriptorHeap* heaps[] = { p->cbvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetPipelineState(mode == 0 ? p->overlayDepthPso.Get() : p->overlayTopPso.Get());
    cmd->SetGraphicsRootSignature(p->rootSignature.Get());
    BindShadowTable(cmd, p);
    cmd->SetGraphicsRootConstantBufferView(0, p->constantBuffer->GetGPUVirtualAddress());
    cmd->SetGraphicsRootConstantBufferView(1, p->sceneBuffer->GetGPUVirtualAddress());

    unsigned int ti = (textureId >= 0 && (unsigned int)textureId < p->textureCount) ? (unsigned int)textureId : 0;
    D3D12_GPU_DESCRIPTOR_HANDLE srv = p->cbvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += (UINT64)ti * p->srvIncrement;
    cmd->SetGraphicsRootDescriptorTable(2, srv);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtvOverrideActive ? p->rtvOverride : rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = GhostGetDSV(p, rhi->GetCurrentSurface());
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VERTEX_BUFFER_VIEW vbv = { p->ovVB->GetGPUVirtualAddress() + (UINT64)p->ovCursor * sizeof(GhostOverlayVertex),
                                     (UINT)(n * sizeof(GhostOverlayVertex)), sizeof(GhostOverlayVertex) };
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &vbv);
    cmd->DrawInstanced((UINT)n, 1, 0, 0);
    p->ovCursor += (unsigned int)n;
}
