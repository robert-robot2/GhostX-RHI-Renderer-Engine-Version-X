// GhostMSAA.h — multisampled scene targets (color + depth per viewport) and the resolve step
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

constexpr DXGI_FORMAT GHOST_MSAA_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;       // matches the swap chain + offscreen targets
constexpr DXGI_FORMAT GHOST_MSAA_DEPTH_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;    // matches the pipeline's depth buffers

struct GhostMSAA
{
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap;       // one slot per surface
    unsigned int                 rtvInc = 0, dsvInc = 0;

    ComPtr<ID3D12Resource>       color[GHOST_MAX_SURFACES];
    ComPtr<ID3D12Resource>       depth[GHOST_MAX_SURFACES];
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};

    unsigned int maxSamples = 1;      // best count the GPU supports for both formats (1, 2, 4 or 8)
    unsigned int active = 1;          // count the PSOs and targets are currently built for
    unsigned int failedFor = 0;       // a count whose PSO rebuild failed (not retried every frame)
};

GhostResult  GhostMSAA_Init(IGhostRHI* rhi, GhostMSAA& m);
void         GhostMSAA_Shutdown(GhostMSAA& m);

// Snaps a requested count to 1, 2, 4 or 8, no higher than the GPU supports
unsigned int GhostMSAA_Clamp(const GhostMSAA& m, int wanted);

// Switches to the wanted count: rebuilds the scene PSOs and frees every target. Between frames only.
GhostResult  GhostMSAA_Apply(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int wanted);

// Per viewport
GhostResult  GhostMSAA_Ensure(IGhostRHI* rhi, GhostMSAA& m, int id, unsigned int w, unsigned int h);
void         GhostMSAA_Release(GhostMSAA& m, int id);

// Begin: points every draw at the multisampled target and clears it.
// End:   resolves it into the offscreen target (intoOffscreen = true, call BEFORE GhostEndOffscreen)
//        or into the window's back buffer (false), and hands drawing back to the 1x targets.
void GhostMSAA_Begin(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id,
    const GhostColor* clear, unsigned int w, unsigned int h);
void GhostMSAA_End(IGhostRHI* rhi, GhostPipeline* p, GhostMSAA& m, int id, bool intoOffscreen);