// GhostFXAA.h — FXAA: tone mapped LDR image -> one pass -> antialiased result in the destination
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

struct GhostFXAA
{
    bool ready = false;
    int  quality = 1;                    // 0 low, 1 default, 2 high

    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;     // one descriptor per surface
    unsigned int                 rtvInc = 0, srvInc = 0;
    ComPtr<ID3D12Resource>       ldr[GHOST_MAX_SURFACES];   // tone mapped image, RGBA8. Rests in PIXEL_SHADER_RESOURCE
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};
};

GhostResult GhostFXAA_Init(IGhostRHI* rhi, GhostFXAA& f);
void        GhostFXAA_Shutdown(GhostFXAA& f);
GhostResult GhostFXAA_Ensure(IGhostRHI* rhi, GhostFXAA& f, int id, unsigned int w, unsigned int h);
void        GhostFXAA_Release(GhostFXAA& f, int id);

// Tone map target: call BeginLdr, tone map into LdrRTV, then Run.
D3D12_CPU_DESCRIPTOR_HANDLE GhostFXAA_LdrRTV(GhostFXAA& f, int id);
void GhostFXAA_BeginLdr(IGhostRHI* rhi, GhostFXAA& f, int id);
// One pass: ldr -> dest (RGBA8, in RENDER_TARGET state)
void GhostFXAA_Run(IGhostRHI* rhi, GhostFXAA& f, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest, unsigned int w, unsigned int h);