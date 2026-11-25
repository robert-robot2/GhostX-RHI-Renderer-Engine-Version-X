// GhostLightShafts.h - screen-space light shafts (Unreal "Light Shaft Bloom / Occlusion" style)
// Put in include/. Self-contained: own root signature, PSO, descriptor heap and scene copy.
#pragma once
#include "IGhostRHI.h"
#include "GhostPipeline.h"     // GHOST_HDR_FORMAT, GHOST_MAX_SURFACES, ComPtr, d3d12

constexpr unsigned int GHOST_MAX_SHAFT_LIGHTS = 4;     // directional lights that can draw shafts in one frame

// One directional light, already projected to the screen by the engine
struct GhostShaftJob
{
    float sunU, sunV;            // light position on screen, 0..1 (may be outside the screen)
    float color[3];              // light colour * intensity * bloom scale * bloom tint * fade
    float length;                // how far toward the light each ray marches (0.1 .. 1)
    float decay;                 // per-step falloff (0.85 .. 1)
    float darkness;              // occluder value (1 = no darkening, like Unreal's Occlusion Mask Darkness)
    float depthRange;            // world units: geometry farther than this does not occlude
    float threshold;             // scene brightness needed to emit light
    float maxBright;             // clamp of the emitted brightness
    int   occlusion;             // 1 = use the depth occlusion mask
    int   steps;                 // samples per pixel
};

struct GhostShafts
{
    bool                         ready = false;
    ComPtr<ID3D12RootSignature>  rs;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12DescriptorHeap> heap;                       // 2 descriptors per surface: scene copy, depth
    unsigned int                 inc = 0;
    ComPtr<ID3D12Resource>       cb;                         // one 256-byte slot per light
    void* cbMapped = nullptr;
    ComPtr<ID3D12Resource>       copy[GHOST_MAX_SURFACES];   // copy of the HDR scene (the pass reads it while writing the real one)
    unsigned int                 cw[GHOST_MAX_SURFACES] = {}, ch[GHOST_MAX_SURFACES] = {};
};

GhostResult GhostShafts_Init(IGhostRHI* rhi, GhostShafts& s);      // not fatal: ready stays false if the shader fails
void        GhostShafts_Release(GhostShafts& s, int surface);      // call from DestroyViewport
void        GhostShafts_Shutdown(GhostShafts& s);

// scene  = the 1x HDR target, must be in RENDER_TARGET state (same place SSAO / SSGI / Fog run). sceneRTV = its RTV.
// depth  = typeless D24S8 depth resting in DEPTH_WRITE (same assumption as GhostDrawFog).
// projA / projB = proj.m[2][2] / proj.m[2][3]. noise = frame noise phase (0 when TAA is off).
bool GhostShafts_Draw(IGhostRHI* rhi, GhostShafts& s, int surface,
    ID3D12Resource* scene, D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV, ID3D12Resource* depth,
    unsigned int w, unsigned int h, float projA, float projB, float noise,
    const GhostShaftJob* jobs, int jobCount);