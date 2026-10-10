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

    // ---- Root Signature — b0 (per-object MVP + color) ----
    D3D12_ROOT_PARAMETER rp[2] = {};

    // b0 — per-object CB, vertex shader only
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[0].Descriptor.ShaderRegister = 0;
    rp[0].Descriptor.RegisterSpace = 0;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    // b1 — scene CB (ambient + lights), pixel shader only
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rp[1].Descriptor.ShaderRegister = 1;
    rp[1].Descriptor.RegisterSpace = 0;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 2;
    rsd.pParameters = rp;
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
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&p->cbvHeap));

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

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(6, 1, 0, 0);       // one quad, corners generated from SV_VertexID
}
void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* p, int meshId, unsigned int slot)
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
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rhi->GetRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = p->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m.vbView);
    cmd->IASetIndexBuffer(&m.ibView);
    cmd->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
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