// GhostPost.h — HDR scene targets and the post-process chain (tone mapping, bloom. SSAO, SSR, SSGI follow)
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

constexpr unsigned int GHOST_BLOOM_MAX_MIPS = 6;
constexpr unsigned int GHOST_POST_SRV_STRIDE = 1 + GHOST_BLOOM_MAX_MIPS;   // per surface: [hdr, bloom mip 0..5]. Same layout in the RTV heap

struct GhostPost
{
    bool ready = false;

    ComPtr<ID3D12RootSignature>  rootSig;                    // tone map
    ComPtr<ID3D12PipelineState>  tonemapPso;                 // HDR (+ bloom) -> RGBA8 destination
    ComPtr<ID3D12RootSignature>  bloomRootSig;
    ComPtr<ID3D12PipelineState>  bloomPrefilterPso, bloomDownPso, bloomUpPso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, srvHeap;
    unsigned int                 rtvInc = 0, srvInc = 0;

    ComPtr<ID3D12Resource>       hdr[GHOST_MAX_SURFACES];    // 1x RGBA16F scene color. Rests in PIXEL_SHADER_RESOURCE
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};

    ComPtr<ID3D12Resource>       bloom[GHOST_MAX_SURFACES];  // half-res RGBA16F mip chain, every mip rests in PIXEL_SHADER_RESOURCE
    unsigned int                 bloomW[GHOST_MAX_SURFACES] = {}, bloomH[GHOST_MAX_SURFACES] = {}, bloomLevels[GHOST_MAX_SURFACES] = {};
    bool                         bloomValid[GHOST_MAX_SURFACES] = {};     // the chain was produced this frame: the tone map adds it
    ComPtr<ID3D12Resource>       fogCopy[GHOST_MAX_SURFACES];   // copy of the scene colour the fog pass reads (created on first use)
    // ---- SSAO (optional: ssaoReady stays false if its shaders fail) ----
    bool                         ssaoReady = false;
    ComPtr<ID3D12RootSignature>  ssaoRootSig;
    ComPtr<ID3D12PipelineState>  ssaoPso, ssaoBlurPso, ssaoCompPso;
    ComPtr<ID3D12DescriptorHeap> ssaoRtvHeap, ssaoSrvHeap;        // per surface: SRV [depth, scene copy, AO A, AO B], RTV [AO A, AO B]
    unsigned int                 ssaoRtvInc = 0, ssaoSrvInc = 0;
    ComPtr<ID3D12Resource>       ssaoA[GHOST_MAX_SURFACES], ssaoB[GHOST_MAX_SURFACES];
    unsigned int                 ssaoW[GHOST_MAX_SURFACES] = {}, ssaoH[GHOST_MAX_SURFACES] = {};
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

// Bloom: HDR (resting in PIXEL_SHADER_RESOURCE) -> half-res chain. Call before the tone map. Returns true if a chain was produced.
bool GhostPost_Bloom(IGhostRHI* rhi, GhostPost& t, int id, unsigned int w, unsigned int h, const GhostPostSettings& s);

// HDR (+ bloom) -> destination (backbuffer or offscreen RTV, RGBA8, in RENDER_TARGET state)
void GhostPost_Tonemap(IGhostRHI* rhi, GhostPost& t, int id, D3D12_CPU_DESCRIPTOR_HANDLE dest,
    unsigned int w, unsigned int h, const GhostPostSettings& s);

// Volumetric fog. color = the scene colour target in RENDER_TARGET state (it is copied, then drawn over with the fogged result),
// colorRTV = its RTV, depth = typeless D24S8 in DEPTH_WRITE. Everything is returned to the same states.
void GhostPost_Fog(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id, ID3D12Resource* color,
    D3D12_CPU_DESCRIPTOR_HANDLE colorRTV, ID3D12Resource* depth, unsigned int w, unsigned int h);

// SSAO on the scene colour. Same contract as GhostPost_Fog: color is in RENDER_TARGET state (copied, then drawn over),
// depth is typeless D24S8 in DEPTH_WRITE. Everything is returned to the same states.
// proj6 = { p00, p11, p02, p12, p22, p23 } of the matrix the scene was drawn with (jittered under TAA).
void GhostPost_SSAO(IGhostRHI* rhi, GhostPost& t, int id, ID3D12Resource* color, D3D12_CPU_DESCRIPTOR_HANDLE colorRTV,
    ID3D12Resource* depth, unsigned int w, unsigned int h, const GhostPostSettings& s, const float* proj6, float noiseOffset);