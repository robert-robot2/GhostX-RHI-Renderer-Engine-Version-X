// GhostPipeline.h
#pragma once
#include "../include/GhostMath.h"
#include "../include/Ghost3D1.h"
#include "../include/IGhostRHI.h"
#include "../include/GhostScene.h"
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct GhostVertex {
    float x, y, z;       // POSITION
    float r, g, b, a;    // COLOR
    float nx, ny, nz;    // NORMAL
    float u, v;          // TEXCOORD
};
struct GhostObjectCB            // matches cbuffer MVPBuffer in GhostVS.hlsl
{
    GhostMat4 model;
    GhostMat4 view;
    GhostMat4 proj;
    float     color[4];
    float     useVertexColor;
    float     pad[3];
};                              // 224 bytes, padded to 256 in the buffer
struct GhostGPULight                // matches struct Light in GhostPS.hlsl
{
    float posRange[4];              // xyz position, w range
    float dirType[4];               // xyz direction (travel dir / facing), w type: 0 dir, 1 point, 2 rect
    float colorIntensity[4];        // rgb color, w intensity
    float rectRight[4];             // xyz right axis, w half-width
    float rectUp[4];                // xyz up axis, w half-height
};                                  // 80 bytes

struct GhostSceneCB                 // matches cbuffer SceneBuffer (b1)
{
    float        ambient[4];        // rgb = color * intensity
    unsigned int lightCount;
    unsigned int pad[3];
    GhostGPULight lights[GHOST_MAX_LIGHTS];
};                                  // 32 + 16*80 = 1312 bytes

constexpr unsigned long long GHOST_SCENE_CB_SIZE = (sizeof(GhostSceneCB) + 255) & ~255ULL;
constexpr unsigned int       GHOST_MAX_OBJECTS = 256;
constexpr unsigned long long GHOST_OBJECT_CB_STRIDE = (sizeof(GhostObjectCB) + 255) & ~255ULL;

struct GhostMesh
{
    ComPtr<ID3D12Resource>   vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    D3D12_INDEX_BUFFER_VIEW  ibView = {};
    unsigned int             indexCount = 0;
};

struct GhostPipeline
{
    ComPtr<ID3D12RootSignature>  rootSignature;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12PipelineState> iconPso;
    GhostMesh meshes[GHOST_MESH_COUNT];

    // ---- Constant Buffer ----
    ComPtr<ID3D12Resource>       constantBuffer;
    ComPtr<ID3D12DescriptorHeap> cbvHeap;
    void* cbvMappedData = nullptr;
    // Scene Buffer
    ComPtr<ID3D12Resource> sceneBuffer;
    void* sceneMappedData = nullptr;
    // ---- Depth Buffer ----
    ComPtr<ID3D12Resource>       depthBuffer;
    ComPtr<ID3D12DescriptorHeap> dsvHeap; 

    D3D12_VIEWPORT viewport = {};
    D3D12_RECT     scissorRect = {};

};


struct GhostPipelineDesc { unsigned int width = 1280; unsigned int height = 720; };

// ---- Lifecycle ----
GhostResult GhostPipelineCreate(IGhostRHI* rhi, const GhostPipelineDesc* desc, GhostPipeline** outPipeline);
void        GhostPipelineDestroy(GhostPipeline* pipeline);

GhostResult GhostUploadMeshes(IGhostRHI* rhi, GhostPipeline* pipeline);
void GhostUpdateObjectCB(GhostPipeline* pipeline, unsigned int slot, const GhostObjectCB* cb);
void GhostUpdateSceneCB(GhostPipeline* pipeline, const GhostSceneCB* cb);
void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId, unsigned int slot);

void GhostDrawIcon(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int slot);

// ---- Resize ----
GhostResult GhostResizeDepthBuffer(IGhostRHI* rhi, GhostPipeline* pipeline,
    unsigned int width, unsigned int height);