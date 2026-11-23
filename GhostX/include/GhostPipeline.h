// GhostPipeline.h
#pragma once
#include "GhostMath.h"
#include "Ghost3D1.h"
#include "IGhostRHI.h"
#include "GhostScene.h"
#include "GhostText.h"
#include "GhostOverlay.h"
#include <vector>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

constexpr DXGI_FORMAT GHOST_HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;   // scene color: gamma-encoded floats, values above 1.0 are kept
constexpr unsigned int GHOST_MAX_PROBES = 8;                                // Reflection Captures sampled by the scene shader at once
constexpr unsigned int GHOST_MAX_FOGVOLS = 8;                               // Fog Volumes the fog pass sees at once

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
    float     pbr[8];
    float     alphaP[4];                  // x blend mode, y alpha source, z cutoff
    float     mapUV[4];                   // extra maps: tiling u,v / offset u,v
    float     maps[GHOST_MAP_COUNT][4];   // per map: x texture id (0 none), y amount, z channel, w invert
    float     surfA[4];                   // x two sided, y IOR, z reflection rotation (rad), w anisotropy
    float     surfB[4];                   // subsurface rgb, w amount
    float     wpoP[4];                    // x sway, y speed, z mid level, w time (s)
    float     opacityP[4];                // x opacity, y Tile Map shadow light selector
    float     tileP[8];                   // Tile Map: [0] weight texture A, [1] weight texture B, [2] layer of this pass, [4] gridX, [5] gridY, [6] 1 = checker look
};
static_assert(sizeof(GhostObjectCB) == 880, "GhostObjectCB layout must match the HLSL cbuffer");

constexpr unsigned int GHOST_SHADOW_SIZE = 1024;     // each shadow map is 1024 x 1024
constexpr unsigned int GHOST_SHADOW_SLICES = 16;       // a directional light uses 1, a point or rect light 6

struct GhostGPULight                // matches struct Light in GhostPS.hlsl
{
    float posRange[4];              // xyz position, w range
    float dirType[4];               // xyz direction (travel dir / facing), w type: 0 dir, 1 point, 2 rect
    float colorIntensity[4];        // rgb color, w intensity
    float rectRight[4];             // xyz right axis, w half-width
    float rectUp[4];                // xyz up axis, w half-height
    float shadow[4];                // x first map slice (-1 = no shadow), y far, z texel size, w god rays strength (0 = none)
};                                  // 96 bytes

struct GhostGPUProbe                // matches struct Probe in GhostPS.hlsl (96 bytes)
{
    float row0[4], row1[4], row2[4];   // world -> volume space: xyz = rotation row, w = translation
    float shape[4];                    // x 0 box / 1 sphere, yzw = half extents (sphere: y = radius)
    float params[4];                   // x blend distance, y intensity, z last mip index, w ambient diffuse amount
    float misc[4];                     // x parallax correction (0/1), y cube slot
};

struct GhostGPUFogVol               // matches struct FogVol in GhostPS.hlsl (128 bytes)
{
    float row0[4], row1[4], row2[4];   // world -> volume space
    float shape[4];                    // x 0 box / 1 sphere, yzw half extents (sphere: y = radius)
    float dens[4];                     // x density, y blend distance, z gradient, w unused
    float color[4];                    // rgb linear colour, w light scatter amount
    float noise[4];                    // x texture id, y amount, z scale
    float scroll[4];                   // xy noise offset
};

struct GhostSceneCB                 // matches cbuffer SceneBuffer (b1)
{
    float        ambient[4];        // rgb = color * intensity
    unsigned int lightCount;
    unsigned int pad[3];
    float        shadowGlobal[4];   // x strength (0 = off), y map size
    float        shadowTune[4];     // x filter mode, y softness (texels), z depth bias scale, w normal bias scale
    GhostGPULight lights[GHOST_MAX_LIGHTS];
    float        shadowVP[GHOST_SHADOW_SLICES][16];     // (existing)
    float        flickGlobal[4];                        // x = time (s)
    float        flick[GHOST_MAX_LIGHTS * GHOST_FLICKER_LAYERS * 3][4];   // per light, per layer: colour+intensity / texture / surface response
    float        probeInfo[4];                          // x = probe count
    GhostGPUProbe probes[GHOST_MAX_PROBES];
    float        fogA[4], fogB[4], fogC[4], fogD[4], fogE[4], fogF[4], fogG[4];
    float        fogCam[6][4];
    GhostGPUFogVol fogVols[GHOST_MAX_FOGVOLS];
    float        ssrA[4], ssrB[4], ssrC[4], ssrD[4];     // see GhostPS.hlsl
    float        ssrView[3][4];                           // previous frame's view matrix, rows 0..2

};                               // 32 + 16*80 = 1312 bytes


// does this line get deleted as well?
constexpr unsigned long long GHOST_SCENE_CB_SIZE = (sizeof(GhostSceneCB) + 255) & ~255ULL;


constexpr unsigned int       GHOST_MAX_OBJECTS = 2048;          // was 1024: draw slots
constexpr unsigned long long GHOST_OBJECT_CB_STRIDE = 1024;
static_assert(sizeof(GhostObjectCB) <= GHOST_OBJECT_CB_STRIDE, "GhostObjectCB too large");

constexpr unsigned int       GHOST_MAX_TEXTURES = 1024;
constexpr unsigned int       GHOST_MAX_SKINS = 16;              // animated models drawn at once

constexpr unsigned int GHOST_MAX_TILEMAPS = 16;                                   // Tile Maps with their own generated mesh
constexpr unsigned int GHOST_TILE_MESH_BASE = GHOST_MAX_MESHES + GHOST_MAX_SKINS;   // first Tile Map slot in meshes[]

constexpr unsigned int GHOST_MAX_SHADOW_CASTERS = 512;          // was 256: this is also the object limit
constexpr unsigned int GHOST_SHADOW_SRV_INDEX = GHOST_MAX_TEXTURES + GHOST_MAX_SURFACES;   // last slot of the SRV heap

/*
constexpr unsigned int GHOST_PROBE_SRV_BASE = GHOST_SHADOW_SRV_INDEX + 1;                  // probe cubemaps: heap slots right after the shadow map (t2..t9)
constexpr unsigned int GHOST_FOG_SRV_BASE = GHOST_PROBE_SRV_BASE + GHOST_MAX_PROBES;       // t10 = scene depth, t11 = scene colour copy (the fog pass writes both every frame)
constexpr unsigned int GHOST_DATA_SRV_BASE = GHOST_FOG_SRV_BASE + 2;                       // linear (UNORM) view of every texture, same order as the ids

constexpr unsigned int GHOST_SSR_SRV_BASE = GHOST_FOG_SRV_BASE + 2;                        // t12 = SSR history colour, t13 = SSR history view depth
constexpr unsigned int GHOST_DATA_SRV_BASE = GHOST_SSR_SRV_BASE + 2;                       // linear (UNORM) view of every texture, same order as the ids
*/

constexpr unsigned int GHOST_PROBE_SRV_BASE = GHOST_SHADOW_SRV_INDEX + 1;  // t2..t9

constexpr unsigned int GHOST_FOG_SRV_BASE =
GHOST_PROBE_SRV_BASE + GHOST_MAX_PROBES;                               // t10..t11

constexpr unsigned int GHOST_SSR_SRV_BASE =
GHOST_FOG_SRV_BASE + 2;                                                // t12..t13

constexpr unsigned int GHOST_DATA_SRV_BASE =
GHOST_SSR_SRV_BASE + 2;                                                // t14 onward


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

constexpr unsigned int GHOST_OVERLAY_MAX_VERTS = 120000;

struct GhostPipeline
{
    ComPtr<ID3D12RootSignature>  rootSignature;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12PipelineState>  blendPso;      // translucent materials
    ComPtr<ID3D12PipelineState> addPso;         // Tile Map layers 1..n: added on top of layer 0 (same depth)
    ComPtr<ID3D12PipelineState> iconPso;
    GhostMesh meshes[GHOST_MAX_MESHES + GHOST_MAX_SKINS + GHOST_MAX_TILEMAPS];   // then skin slots, then Tile Map slots
    void* tileVBMapped[GHOST_MAX_TILEMAPS] = {};     // Tile Map meshes keep their upload buffers mapped, so sculpting only copies bytes
    void* tileIBMapped[GHOST_MAX_TILEMAPS] = {};
    unsigned int tileVBCap[GHOST_MAX_TILEMAPS] = {}, tileIBCap[GHOST_MAX_TILEMAPS] = {};
    ComPtr<ID3D12Resource> splatUpload;                      // shared upload buffer for weight map updates
    unsigned long long     splatUploadSize = 0;
    void* skinMapped[GHOST_MAX_SKINS] = {};

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

    ComPtr<ID3D12Resource> blitCB;              // blit pass constants (GhostObjectCB layout)
    void* blitCBMapped = nullptr;

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

    ComPtr<ID3D12PipelineState>  overlayDepthPso, overlayTopPso;
    ComPtr<ID3D12Resource>       ovVB;
    void* ovMapped = nullptr;
    unsigned int                 ovCursor = 0;

    ComPtr<ID3D12PipelineState>  skyPso;
    ComPtr<ID3D12PipelineState>  fogPso;                   // volumetric fog (null if GhostFog.hlsl failed to compile: fog stays off)
    // ---- Shadows ----
    ComPtr<ID3D12RootSignature>  shadowRootSig;
    ComPtr<ID3D12PipelineState>  shadowPso;
    ComPtr<ID3D12Resource>       shadowMap;            // Texture2DArray, GHOST_SHADOW_SLICES slices
    ComPtr<ID3D12DescriptorHeap> shadowDsvHeap;
    unsigned int                 shadowDsvInc = 0;
    ComPtr<ID3D12Resource>       shadowCB;             // per-caster constant buffers (GhostObjectCB layout)
    void* shadowCBMapped = nullptr;
    unsigned int                 shadowSize = GHOST_SHADOW_SIZE;   // current map size per side (runtime)

    ComPtr<ID3D12Resource>       uiVB;
    void* uiMapped = nullptr;
    bool                         rtvOverrideActive = false;

    D3D12_CPU_DESCRIPTOR_HANDLE  rtvOverride = {};
    // ---- MSAA ----
    unsigned int                 sampleCount = 1;          // sample count the scene PSOs were built for
    bool                         dsvOverrideActive = false;
    D3D12_CPU_DESCRIPTOR_HANDLE  dsvOverride = {};
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

// Animated models: a CPU-writable vertex buffer that shares the static mesh's index buffer.
// Draw it with meshId = GHOST_MAX_MESHES + slot.
GhostResult GhostCreateSkinMesh(IGhostRHI* rhi, GhostPipeline* pipeline, int slot, int srcMeshId, unsigned int vertexCount);
void* GhostSkinMapped(GhostPipeline* pipeline, int slot);
void        GhostReleaseSkinMesh(GhostPipeline* pipeline, int slot);

// Tile Maps: a generated mesh (32-bit indices) in slot 0..GHOST_MAX_TILEMAPS-1. Draw with meshId = GHOST_TILE_MESH_BASE + slot.
// Re-uploading replaces the old buffers: call between frames only.
GhostResult GhostUploadTileMesh(IGhostRHI* rhi, GhostPipeline* pipeline, int slot,
    const GhostVertex* vertices, unsigned int vertexCount, const unsigned int* indices, unsigned int indexCount);
void        GhostReleaseTileMesh(GhostPipeline* pipeline, int slot);
// Rebuilds the PSOs that draw into the scene target (main, icon, UI, overlays, sky) at a new sample count.
// Between frames only. On failure nothing is changed.
GhostResult GhostRebuildScenePSOs(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int sampleCount);

void GhostUpdateObjectCB(GhostPipeline* pipeline, unsigned int slot, const GhostObjectCB* cb);
void GhostUpdateSceneCB(GhostPipeline* pipeline, const GhostSceneCB* cb);
// rgba = tightly packed RGBA8, top row first. Builds mips, uploads, creates the SRV. Returns texture id or -1.
// replaceIndex >= 0 re-uploads into that existing slot (same id) instead of adding a new texture
int GhostCreateTexture(IGhostRHI* rhi, GhostPipeline* pipeline,
    const unsigned char* rgba, unsigned int width, unsigned int height, int replaceIndex = -1);

// indexCount 0 = whole mesh; otherwise draws indices [firstIndex, firstIndex + indexCount)
void GhostDrawMesh(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId, unsigned int slot,
    unsigned int textureIndex = 0, unsigned int firstIndex = 0, unsigned int indexCount = 0,
    bool translucent = false, bool additive = false);

// Re-uploads the top mip of an existing texture (weight maps). Same size only. Returns false on failure.
bool GhostUpdateTextureLevel0(IGhostRHI* rhi, GhostPipeline* pipeline, int index,
    const unsigned char* rgba, unsigned int width, unsigned int height);

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
    float x, float y, float w, float h, int pointFilter = 0);

// Draws the quad list into the current render target. scale = reference units -> pixels.
void GhostDrawUI(IGhostRHI* rhi, GhostPipeline* pipeline, const std::vector<GhostUIQuad>& quads,
    unsigned int w, unsigned int h, float scale);

void GhostDrawSky(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int slot, unsigned int textureIndex);

// Volumetric fog: reads a copy of the scene colour + the scene depth, writes the fogged image into dstRTV (1x HDR format).
// depth = typeless D24S8 resting in DEPTH_WRITE (moved to PIXEL_SHADER_RESOURCE and back). Not for use inside a render pass override.
void GhostDrawFog(IGhostRHI* rhi, GhostPipeline* p, ID3D12Resource* sceneCopy, ID3D12Resource* depth,
    D3D12_CPU_DESCRIPTOR_HANDLE dstRTV, unsigned int w, unsigned int h);

// ---- Shadow pass ----
void GhostUpdateShadowCB(GhostPipeline* pipeline, unsigned int slot, const GhostObjectCB* cb);
void GhostShadowBegin(IGhostRHI* rhi, GhostPipeline* pipeline);
void GhostShadowSlice(IGhostRHI* rhi, GhostPipeline* pipeline, int slice,
    const float* vp16, const float* posFar4, float mode);       // mode 0 = directional, 1 = point
void GhostShadowDraw(IGhostRHI* rhi, GhostPipeline* pipeline, int meshId, unsigned int slot, unsigned int textureIndex);
void GhostShadowEnd(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int w, unsigned int h);
GhostResult GhostSetShadowSize(IGhostRHI* rhi, GhostPipeline* pipeline, unsigned int size);   // between frames only

// ---- Viewport overlay (grid, gizmos, dimensions, text) ----
void GhostOverlayBegin(GhostPipeline* pipeline);               // once per viewport frame
// mode 0 = depth tested, 1 = always on top. textureId 0 = plain colour, or the font atlas for text.
void GhostDrawOverlay(IGhostRHI* rhi, GhostPipeline* pipeline, const std::vector<GhostOverlayVertex>& verts,
    int mode, int textureId);