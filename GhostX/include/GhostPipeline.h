// GhostPipeline.h
#pragma once
#include "../include/GhostMath.h"
#include "../include/Ghost3D1.h"
#include "../include/IGhostRHI.h"
#include "../include/GhostScene.h"
#include "../include/GhostText.h"
#include <vector>
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
struct GhostObjectCB            // matches cbuffer MVPBuffer (VS) and ObjectBuffer (PS), both at b0
{
    GhostMat4 model;
    GhostMat4 view;
    GhostMat4 proj;
    float     color[4];         // material tint (rgb)
    float     useVertexColor;
    float     pad[3];           // pad[0] = selected tint (VS)
    float     uvParams[4];      // tilingU, tilingV, offsetU, offsetV
    float     mapParams[4];     // mode, rotation (radians), wrap, filter
    float     sky[64];          // sky pass parameters (16 float4); zero for every other draw
    float     pbr[8];           // [0] metallic, [1] roughness, [2] specular, [3] emissive strength,
    // [4..6] emissive rgb, [7] = 1 when the draw uses PBR (sprites leave it 0)
};
static_assert(sizeof(GhostObjectCB) == 544, "GhostObjectCB layout must match the HLSL cbuffer");
constexpr unsigned int GHOST_SHADOW_SIZE = 1024;     // each shadow map is 1024 x 1024
constexpr unsigned int GHOST_SHADOW_SLICES = 16;       // a directional light uses 1, a point or rect light 6

struct GhostGPULight                // matches struct Light in GhostPS.hlsl
{
    float posRange[4];              // xyz position, w range
    float dirType[4];               // xyz direction (travel dir / facing), w type: 0 dir, 1 point, 2 rect
    float colorIntensity[4];        // rgb color, w intensity
    float rectRight[4];             // xyz right axis, w half-width
    float rectUp[4];                // xyz up axis, w half-height
    float shadow[4];                // x first map slice (-1 = no shadow), y far (world units), z texel size, w unused
};                                  // 96 bytes

struct GhostSceneCB                 // matches cbuffer SceneBuffer (b1)
{
    float        ambient[4];        // rgb = color * intensity
    unsigned int lightCount;
    unsigned int pad[3];
    float        shadowGlobal[4];   // x strength (0 = off), y map size
    GhostGPULight lights[GHOST_MAX_LIGHTS];
    float        shadowVP[GHOST_SHADOW_SLICES][16];     // light view-projection per slice
};                               // 32 + 16*80 = 1312 bytes


// does this line get deleted as well?
constexpr unsigned long long GHOST_SCENE_CB_SIZE = (sizeof(GhostSceneCB) + 255) & ~255ULL;


constexpr unsigned int       GHOST_MAX_OBJECTS = 1024;
constexpr unsigned long long GHOST_OBJECT_CB_STRIDE = 768;     // room for the Section B material block
static_assert(sizeof(GhostObjectCB) <= GHOST_OBJECT_CB_STRIDE, "GhostObjectCB too large");

constexpr unsigned int       GHOST_MAX_TEXTURES = 256;
constexpr unsigned int GHOST_MAX_SHADOW_CASTERS = 256;
constexpr unsigned int GHOST_SHADOW_SRV_INDEX = GHOST_MAX_TEXTURES + GHOST_MAX_SURFACES;   // last slot of the SRV heap

struct GhostMesh
{
    ComPtr<ID3D12Resource>   vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbView = {};
    D3D12_INDEX_BUFFER_VIEW  ibView = {};
    unsigned int             indexCount = 0;
    std::vector<GhostVertex>  cpuVerts;       // imported meshes only: a CPU copy for picking
    std::vector<unsigned int> cpuIdx;
};

// Read access to a mesh's triangles on the CPU, whatever the index width
struct GhostMeshCPU
{
    const GhostVertex* verts = nullptr;
    unsigned int          vertexCount = 0;
    const unsigned short* idx16 = nullptr;
    const unsigned int* idx32 = nullptr;
    unsigned int          indexCount = 0;
    unsigned int Index(unsigned int i) const { return idx32 ? idx32[i] : idx16[i]; }
};

struct GhostPipeline
{
    ComPtr<ID3D12RootSignature>  rootSignature;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12PipelineState> iconPso;
    GhostMesh meshes[GHOST_MAX_MESHES];

    // ---- Constant Buffer ----
    ComPtr<ID3D12Resource>       constantBuffer;
    ComPtr<ID3D12DescriptorHeap> cbvHeap;
    void* cbvMappedData = nullptr;
    // Textures
    ComPtr<ID3D12Resource> textures[GHOST_MAX_TEXTURES];
    unsigned int           textureCount = 0;
    unsigned int           srvIncrement = 0;
    // Scene Buffer
    ComPtr<ID3D12Resource> sceneBuffer;
    void* sceneMappedData = nullptr;
    // ---- Depth Buffer ----
    ComPtr<ID3D12Resource>       depthBuffers[GHOST_MAX_SURFACES];
    unsigned int                 dsvIncrement = 0;
    unsigned int                 depthW[GHOST_MAX_SURFACES] = {}, depthH[GHOST_MAX_SURFACES] = {};

    // ---- Offscreen render targets (one per surface, only used for fixed resolutions) ----
    ComPtr<ID3D12DescriptorHeap> offRtvHeap;
    ComPtr<ID3D12Resource>       offTargets[GHOST_MAX_SURFACES];
    unsigned int                 offW[GHOST_MAX_SURFACES] = {}, offH[GHOST_MAX_SURFACES] = {};
    unsigned int                 rtvIncrement = 0;
    ComPtr<ID3D12PipelineState>  blitPso;
    ComPtr<ID3D12PipelineState>  uiPso;
    ComPtr<ID3D12PipelineState>  skyPso;
    // ---- Shadows ----
    ComPtr<ID3D12RootSignature>  shadowRootSig;
    ComPtr<ID3D12PipelineState>  shadowPso;
    ComPtr<ID3D12Resource>       shadowMap;            // Texture2DArray, GHOST_SHADOW_SLICES slices
    ComPtr<ID3D12DescriptorHeap> shadowDsvHeap;
    unsigned int                 shadowDsvInc = 0;
    ComPtr<ID3D12Resource>       shadowCB;             // per-caster constant buffers (GhostObjectCB layout)
    void* shadowCBMapped = nullptr;

    ComPtr<ID3D12Resource>       uiVB;
    void* uiMapped = nullptr;
    bool                         rtvOverrideActive = false;

    D3D12_CPU_DESCRIPTOR_HANDLE  rtvOverride = {};
    ComPtr<ID3D12DescriptorHeap> dsvHeap; 

    D3D12_VIEWPORT viewport = {};
    D3D12_RECT     scissorRect = {};

};


struct GhostPipelineDesc { unsigned int width = 1280; unsigned int height = 720; };

// ---- Lifecycle ----
GhostResult GhostPipelineCreate(IGhostRHI* rhi, const GhostPipelineDesc* desc, GhostPipeline** outPipeline);
void        GhostPipelineDestroy(GhostPipeline* pipeline);

// CPU-side mesh data (picking only). Returns false for an unknown mesh id.
bool GhostGetMeshCPU(GhostPipeline* pipeline, int meshId, GhostMeshCPU* out);

// Uploads an imported mesh into registry slot `meshId` (>= GHOST_MESH_COUNT) with 32-bit indices
GhostResult GhostUploadMeshData(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId,
    const void* vertices, unsigned int vertexCount, const unsigned int* indices, unsigned int indexCount);

GhostResult GhostUploadMeshes(IGhostRHI* rhi, GhostPipeline* pipeline);


void GhostUpdateObjectCB(GhostPipeline* pipeline, unsigned int slot, const GhostObjectCB* cb);
void GhostUpdateSceneCB(GhostPipeline* pipeline, const GhostSceneCB* cb);
// rgba = tightly packed RGBA8, top row first. Builds mips, uploads, creates the SRV. Returns texture id or -1.
// replaceIndex >= 0 re-uploads into that existing slot (same id) instead of adding a new texture
int GhostCreateTexture(IGhostRHI* rhi, GhostPipeline* pipeline,
    const unsigned char* rgba, unsigned int width, unsigned int height, int replaceIndex = -1);

// indexCount 0 = whole mesh; otherwise draws indices [firstIndex, firstIndex + indexCount)
void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId, unsigned int slot,
    unsigned int textureIndex = 0, unsigned int firstIndex = 0, unsigned int indexCount = 0);

void GhostDrawIcon(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int slot);

// ---- Resize ----
GhostResult GhostResizeDepthBuffer(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId,
    unsigned int width, unsigned int height);
D3D12_CPU_DESCRIPTOR_HANDLE GhostGetDSV(GhostPipeline* pipeline, int surfaceId);
void GhostReleaseDepth(GhostPipeline* pipeline, int surfaceId);

// ---- Render resolution ----
GhostResult GhostEnsureDepth(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId, unsigned int w, unsigned int h);
GhostResult GhostEnsureOffscreen(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId, unsigned int w, unsigned int h);
void GhostReleaseOffscreen(GhostPipeline* pipeline, int surfaceId);
D3D12_CPU_DESCRIPTOR_HANDLE GhostGetOffscreenRTV(GhostPipeline* pipeline, int surfaceId);
void GhostBeginOffscreen(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId,
    const GhostColor* clear, unsigned int w, unsigned int h);
void GhostEndOffscreen(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId);
void GhostDrawBlit(IGhostRHI* rhi, GhostPipeline* pipeline, int surfaceId,
    float x, float y, float w, float h);

// Draws the quad list into the current render target. scale = reference units -> pixels.
void GhostDrawUI(IGhostRHI* rhi, GhostPipeline* pipeline, const std::vector<GhostUIQuad>& quads,
    unsigned int w, unsigned int h, float scale);

void GhostDrawSky(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int slot, unsigned int textureIndex);

// ---- Shadow pass ----
void GhostUpdateShadowCB(GhostPipeline* pipeline, unsigned int slot, const GhostObjectCB* cb);
void GhostShadowBegin(IGhostRHI* rhi, GhostPipeline* pipeline);
void GhostShadowSlice(IGhostRHI* rhi, GhostPipeline* pipeline, int slice,
    const float* vp16, const float* posFar4, float mode);       // mode 0 = directional, 1 = point
void GhostShadowDraw(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId, unsigned int slot, unsigned int textureIndex);
void GhostShadowEnd(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int w, unsigned int h);