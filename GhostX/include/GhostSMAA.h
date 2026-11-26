// GhostSMAA.h — SMAA 1x: tone mapped LDR image -> edges -> blend weights -> blended result in the destination
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

struct GhostSMAA
{
    bool ready = false;                 // shaders compiled, lookup textures uploaded
    int  quality = 2;                   // 0 low, 1 medium, 2 high, 3 ultra (the wanted value)
    int  builtQuality = -1;             // the preset the PSOs are built for

    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  edgePso, weightPso, blendPso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
    unsigned int                 rtvInc = 0, srvInc = 0;

    ComPtr<ID3D12Resource>       area, search;                        // lookup tables (160x560 RG8, 64x16 R8)
    ComPtr<ID3D12Resource>       ldr[GHOST_MAX_SURFACES];             // tone mapped image, RGBA8
    ComPtr<ID3D12Resource>       edges[GHOST_MAX_SURFACES];           // RG8
    ComPtr<ID3D12Resource>       blend[GHOST_MAX_SURFACES];           // RGBA8
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};
};

GhostResult GhostSMAA_Init(IGhostRHI* rhi, GhostSMAA& s);
void        GhostSMAA_Shutdown(GhostSMAA& s);
// Rebuilds the PSOs for another preset (between frames). On failure the old ones stay.
GhostResult GhostSMAA_SetQuality(IGhostRHI* rhi, GhostSMAA& s, int quality);

GhostResult GhostSMAA_Ensure(IGhostRHI* rhi, GhostSMAA& s, int id, unsigned int w, unsigned int h);
void        GhostSMAA_Release(GhostSMAA& s, int id);

// Tone map target: call Begin, tone map into LdrRTV, then Run.
D3D12_CPU_DESCRIPTOR_HANDLE GhostSMAA_LdrRTV(GhostSMAA& s, int id);
void GhostSMAA_BeginLdr(IGhostRHI* rhi, GhostSMAA& s, int id);
// Runs the three passes and writes the antialiased image into dest (RGBA8, in RENDER_TARGET state)
void GhostSMAA_Run(IGhostRHI* rhi, GhostSMAA& s, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest, unsigned int w, unsigned int h);