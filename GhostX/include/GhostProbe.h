// GhostProbe.h — Reflection Capture cubemaps: capture targets, roughness mips, custom-texture environments
#pragma once
#include "GhostPipeline.h"
#include "IGhostRHI.h"

constexpr unsigned int GHOST_PROBE_MAX_MIPS = 7;

struct GhostProbeSlot
{
    ComPtr<ID3D12Resource> cube;       // 6 slices x mips, RGBA16F, gamma-encoded like the HDR scene. Every subresource rests in PIXEL_SHADER_RESOURCE
    unsigned int res = 0, mips = 0;
    bool inUse = false;                // a volume owns this slot
    bool valid = false;                // holds a finished capture
};

struct GhostProbeSys
{
    bool ready = false;
    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  prefilterPso, equirectPso;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;      // srvHeap: single-mip cube views for the blur passes + 1 equirect slot
    unsigned int                 rtvInc = 0, dsvInc = 0, srvInc = 0;
    ComPtr<ID3D12Resource>       scratchColor, scratchDepth;     // one face worth of scene target, at the pipeline's sample count
    unsigned int                 scratchRes = 0, scratchSamples = 0;
    GhostProbeSlot               slot[GHOST_MAX_PROBES];
};

GhostResult GhostProbe_Init(IGhostRHI* rhi, GhostProbeSys& s);
void        GhostProbe_Shutdown(GhostProbeSys& s);

int         GhostProbe_AllocSlot(GhostProbeSys& s);                                          // -1 = all slots taken
void        GhostProbe_ReleaseSlot(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot);
GhostResult GhostProbe_EnsureSlot(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, unsigned int res);
GhostResult GhostProbe_EnsureScratch(IGhostRHI* rhi, GhostProbeSys& s, unsigned int res, unsigned int samples);

inline bool         GhostProbe_Valid(const GhostProbeSys& s, int slot) { return slot >= 0 && slot < (int)GHOST_MAX_PROBES && s.slot[slot].valid; }
inline unsigned int GhostProbe_Mips(const GhostProbeSys& s, int slot) { return (slot >= 0 && slot < (int)GHOST_MAX_PROBES) ? s.slot[slot].mips : 0u; }
inline void         GhostProbe_SetValid(GhostProbeSys& s, int slot, bool v) { if (slot >= 0 && slot < (int)GHOST_MAX_PROBES) s.slot[slot].valid = v; }

// Scene capture, per face (call between GhostRHI BeginOffscreenFrame / EndOffscreenFrame):
//   BeginFace: clears the scratch target and points every draw at it. EndFace: resolves / copies it into mip 0 of the cube face.
void GhostProbe_BeginFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, const GhostColor* clear, unsigned int res);
void GhostProbe_EndFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, int face);

// Custom-texture capture: draws a lat-long texture into one cube face (mip 0)
void GhostProbe_EquirectFace(IGhostRHI* rhi, GhostPipeline* p, GhostProbeSys& s, int slot, int face, int textureId, float yawRad);

// Builds mips 1.. from mip 0 (cascade blur). Call after all six faces are written.
void GhostProbe_Prefilter(IGhostRHI* rhi, GhostProbeSys& s, int slot);