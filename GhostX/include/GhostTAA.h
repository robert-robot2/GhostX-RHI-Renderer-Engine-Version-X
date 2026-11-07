// GhostTAA.h — temporal antialiasing: jitter, per-viewport history, depth-based reprojection resolve
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

constexpr unsigned int GHOST_TAA_CONSTS = 48;     // root constants for the resolve pass

// What the engine knows about this viewport's camera this frame (all UNJITTERED)
struct GhostTAAFrame
{
    float eye[3], right[3], up[3], fwd[3];
    float p00, p11, p22, p23;      // projection elements: x scale, y scale, depth terms
    float vp[16];                  // view-projection (proj * view); becomes next frame's "previous"
};

struct GhostTAA
{
    bool ready = false;

    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  pso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
    unsigned int                 rtvInc = 0, dsvInc = 0, srvInc = 0;

    ComPtr<ID3D12Resource>       scene[GHOST_MAX_SURFACES];          // jittered scene color (RGBA8, same as the scene PSOs)
    ComPtr<ID3D12Resource>       hist[2][GHOST_MAX_SURFACES];        // ping-pong history (RGBA16F: no 8-bit stalling)
    ComPtr<ID3D12Resource>       depth[GHOST_MAX_SURFACES];          // typeless D24S8: DSV for drawing, SRV for the resolve
    unsigned int                 w[GHOST_MAX_SURFACES] = {}, h[GHOST_MAX_SURFACES] = {};

    int   read[GHOST_MAX_SURFACES] = {};     // which history holds the latest result
    bool  reset[GHOST_MAX_SURFACES] = {};     // ignore history this frame (new or resized targets)
    bool  havePrev[GHOST_MAX_SURFACES] = {};
    float prevVP[GHOST_MAX_SURFACES][16] = {};
    float phase[GHOST_MAX_SURFACES] = {};     // jitter sequence position (advances by the jitter speed)

    // ---- The "gummy" look. Tweak here. ----
    float feedbackStill = 0.95f;    // history weight when nothing moves (higher = softer, steadier)
    float feedbackMoving = 0.85f;    // history weight on fast-moving pixels (higher = longer ghost trails)
    float clipGamma = 1.6f;     // history clip box size in sigmas (higher = looser = gummier)
    float sharpen = 0.20f;    // output-only luma sharpen (0 = none)
    float velocityScale = 0.125f;   // 1 / (pixels of motion at which the "moving" weight is fully used)
};

GhostResult GhostTAA_Init(IGhostRHI* rhi, GhostTAA& t);
void        GhostTAA_Shutdown(GhostTAA& t);

// Per viewport: (re)creates targets for this render size
GhostResult GhostTAA_Ensure(IGhostRHI* rhi, GhostTAA& t, int id, unsigned int w, unsigned int h);
void        GhostTAA_Release(GhostTAA& t, int id);

// Sub-pixel jitter in pixels, each in [-0.5, 0.5]. speed = steps of the 16-sample sequence per frame.
void GhostTAA_NextJitter(GhostTAA& t, int id, float speed, float& jx, float& jy);

// Begin: points every draw at the jittered scene target + TAA depth and clears them.
// End:   resolves into the history AND the destination (back buffer RTV, or the offscreen RTV),
//        then hands drawing back to the destination so the game UI can draw on top, crisp.
void GhostTAA_Begin(IGhostRHI* rhi, GhostPipeline* p, GhostTAA& t, int id,
    const GhostColor* clear, unsigned int w, unsigned int h);
void GhostTAA_End(IGhostRHI* rhi, GhostPipeline* p, GhostTAA& t, int id,
    D3D12_CPU_DESCRIPTOR_HANDLE dest, bool destIsOffscreen, const GhostTAAFrame& f);
// Drop this viewport's history (call when the render resolution changes)
inline void GhostTAA_ResetHistory(GhostTAA& t, int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES) return;
    t.reset[id] = true;
    t.havePrev[id] = false;
}