// GhostPost.h — HDR scene targets and the post-process chain (Phase 2: tone mapping. Bloom, SSAO, SSR, SSGI follow)
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

struct GhostPost
{
    bool ready = false;
    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  tonemapPso;                 // HDR -> RGBA8 destination
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;           // one slot per surface
    unsigned int                 rtvInc = 0, srvInc = 0;

    ComPtr<ID3D12Resource>       hdr[GHOST_MAX_SURFACES];    // 1x RGBA16F scene color. Rests in PIXEL_SHADER_RESOURCE
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};
};

GhostResult GhostPost_Init(IGhostRHI* rhi, GhostPost& t);
void        GhostPost_Shutdown(GhostPost& t);
GhostResult GhostPost_Ensure(IGhostRHI* rhi, GhostPost& t, int id, unsigned int w, unsigned int h);
void        GhostPost_Release(GhostPost& t, int id);

ID3D12Resource* GhostPost_Resource(GhostPost& t, int id);
D3D12_CPU_DESCRIPTOR_HANDLE GhostPost_RTV(GhostPost& t, int id);
void GhostPost_ToRT(IGhostRHI* rhi, GhostPost& t, int id);      // PIXEL_SHADER_RESOURCE -> RENDER_TARGET
void GhostPost_ToSRV(IGhostRHI* rhi, GhostPost& t, int id);     // RENDER_TARGET -> PIXEL_SHADER_RESOURCE

// Plain path (no MSAA, no TAA): the scene draws straight into the HDR target using the surface's depth buffer
void GhostPost_BeginScene(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id, const GhostColor* clear,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv, unsigned int w, unsigned int h);
void GhostPost_EndScene(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id);

// HDR -> destination (backbuffer or offscreen RTV, RGBA8, in RENDER_TARGET state)
void GhostPost_Tonemap(IGhostRHI* rhi, GhostPost& t, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest,
    unsigned int w, unsigned int h, const GhostPostSettings& s);