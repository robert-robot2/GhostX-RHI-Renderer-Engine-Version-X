// GhostPost.h — HDR scene targets and the post-process chain (tone mapping, bloom. SSAO, SSR, SSGI follow)
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

constexpr unsigned int GHOST_BLOOM_MAX_MIPS = 6;
constexpr unsigned int GHOST_POST_SRV_STRIDE = 1 + GHOST_BLOOM_MAX_MIPS;   // per surface: [hdr, bloom mip 0..5]. Same layout in the RTV heap

// The camera this frame, all UNJITTERED, for the SSGI temporal reprojection
struct GhostSsgiFrame
{
    float eye[3], right[3], up[3], fwd[3];
    float vp[16];                       // proj * view (clean): becomes next frame's "previous"
};

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
    // ---- SSR history (optional: ssrReady stays false if its shader fails) ----
    bool                         ssrReady = false;
    ComPtr<ID3D12RootSignature>  ssrRootSig;
    ComPtr<ID3D12PipelineState>  ssrDepthPso;                 // depth -> view-space depth (R32F)
    ComPtr<ID3D12DescriptorHeap> ssrRtvHeap, ssrSrvHeap;
    unsigned int                 ssrRtvInc = 0, ssrSrvInc = 0;
    ComPtr<ID3D12Resource>       ssrColor[GHOST_MAX_SURFACES];    // last frame's 1x HDR image
    ComPtr<ID3D12Resource>       ssrDepth[GHOST_MAX_SURFACES];    // last frame's view-space depth (1e6 = sky)
    unsigned int                 ssrW[GHOST_MAX_SURFACES] = {}, ssrH[GHOST_MAX_SURFACES] = {};
    bool                         ssrHave[GHOST_MAX_SURFACES] = {};   // the history holds a finished frame
    float                        ssrPrevView[GHOST_MAX_SURFACES][12] = {};   // view matrix rows 0..2 of that frame
    float                        ssrPrevProj[GHOST_MAX_SURFACES][8] = {};    // p00 p11 p02 p12 p22 p23 width height
    // ---- SSGI (optional: ssgiReady stays false if its shaders fail) ----
    bool                         ssgiReady = false;
    ComPtr<ID3D12RootSignature>  ssgiRootSig;
    ComPtr<ID3D12PipelineState>  ssgiGatherPso, ssgiBlurPso, ssgiCompPso;
    ComPtr<ID3D12DescriptorHeap> ssgiRtvHeap, ssgiSrvHeap;      // per surface: SRV [depth, scene, T0, T1, blurA, blurB], RTV [T0, T1, blurA, blurB]
    unsigned int                 ssgiRtvInc = 0, ssgiSrvInc = 0;
    ComPtr<ID3D12Resource>       ssgiT[2][GHOST_MAX_SURFACES];  // half-res gather + temporal history (rgb = bounce, a = view depth)
    ComPtr<ID3D12Resource>       ssgiBlur[2][GHOST_MAX_SURFACES];
    unsigned int                 ssgiW[GHOST_MAX_SURFACES] = {}, ssgiH[GHOST_MAX_SURFACES] = {};
    int                          ssgiRead[GHOST_MAX_SURFACES] = {};        // which of ssgiT holds the latest result
    bool                         ssgiHave[GHOST_MAX_SURFACES] = {};        // the history holds a finished frame
    float                        ssgiPrevVP[GHOST_MAX_SURFACES][16] = {};
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

// SSR history. UpdateTargets creates the history for this surface while SSR is wanted, and frees it otherwise.
void GhostPost_SSRUpdateTargets(IGhostRHI* rhi, GhostPost& t, int id, bool wanted, unsigned int w, unsigned int h);
// Points the shared t12 / t13 descriptors at this surface's history (null descriptors when it has none). Every viewport calls this before drawing.
void GhostPost_SSRBind(IGhostRHI* rhi, GhostPipeline* p, GhostPost& t, int id);
// End of the frame: copies the finished HDR image (PIXEL_SHADER_RESOURCE) and the depth (typeless D24S8, DEPTH_WRITE) into the history.
// view12 = view matrix rows 0..2, proj8 = p00 p11 p02 p12 p22 p23 width height (clean, unjittered). States are restored.
void GhostPost_SSRCapture(IGhostRHI* rhi, GhostPost& t, int id, ID3D12Resource* color, ID3D12Resource* depth,
    unsigned int w, unsigned int h, const float* view12, const float* proj8);

inline bool GhostPost_SSRHistoryValid(const GhostPost& t, int id)
{
    return id >= 0 && id < GHOST_MAX_SURFACES && t.ssrHave[id] && t.ssrColor[id] && t.ssrDepth[id];
}
inline void GhostPost_SSRReset(GhostPost& t) { for (int i = 0; i < GHOST_MAX_SURFACES; i++) t.ssrHave[i] = false; }   // scene changed: drop the history

// SSGI on the scene colour. Same contract as GhostPost_SSAO: color is in RENDER_TARGET state (copied, then drawn over),
// depth is typeless D24S8 in DEPTH_WRITE. Everything is returned to the same states.
// proj6 = { p00, p11, p02, p12, p22, p23 } of the matrix the scene was drawn with (jittered under TAA).
void GhostPost_SSGI(IGhostRHI* rhi, GhostPost& t, int id, ID3D12Resource* color, D3D12_CPU_DESCRIPTOR_HANDLE colorRTV,
    ID3D12Resource* depth, unsigned int w, unsigned int h, const GhostPostSettings& s, const float* proj6,
    const GhostSsgiFrame& f, float noiseOffset);

inline void GhostPost_SSGIReset(GhostPost& t) { for (int i = 0; i < GHOST_MAX_SURFACES; i++) t.ssgiHave[i] = false; }