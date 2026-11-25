// GhostGpuProf.h - tiny GPU timestamp profiler. Header only.
#pragma once
#include "IGhostRHI.h"
#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <string.h>

enum GhostProfMark
{
    GP_BEGIN = 0,   // right after BeginFrame
    GP_PRESHADOW,   // before the shadow pass            (segment 1 = target clears / setup)
    GP_SHADOW,      // after the shadow pass             (segment 2 = shadows)
    GP_SCENE,       // after sky + objects + particles   (segment 3 = scene: lighting, SSR, terrain)
    GP_RESOLVE,     // after the MSAA resolve            (segment 4 = MSAA resolve, 0 when MSAA is off)
    GP_POSTFX,      // after AO / GI / fog / TAA         (segment 5 = AO, GI, fog, TAA)
    GP_TONEMAP,     // after SSR capture, bloom, tone map(segment 6)
    GP_END,         // just before EndFrame              (segment 7 = overlays, UI, blit)
    GP_COUNT
};

struct GhostGpuProf
{
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource>  readback;
    UINT64        freq = 0;
    LARGE_INTEGER qpcFreq = {}, t0 = {};
    bool  ready = false, active = false, valid = false;
    bool  marked[GP_COUNT] = {};
    float seg[GP_COUNT] = {};          // smoothed ms of the segment that ENDS at this mark
    float total = 0.0f, cpuMs = 0.0f;  // GPU total, CPU command recording time (the GPU wait is not included)
};

inline void GhostProf_Init(IGhostRHI* rhi, GhostGpuProf& p)
{
    p.ready = false;
    ID3D12Device* dev = rhi ? rhi->GetDevice() : nullptr;
    if (!dev) return;

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> q;                 // only used to ask for the timestamp frequency
    if (FAILED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) || FAILED(q->GetTimestampFrequency(&p.freq)) || p.freq == 0) return;

    D3D12_QUERY_HEAP_DESC hd = {};
    hd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    hd.Count = GP_COUNT;
    if (FAILED(dev->CreateQueryHeap(&hd, IID_PPV_ARGS(&p.heap)))) return;

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = sizeof(UINT64) * GP_COUNT;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&p.readback)))) return;

    QueryPerformanceFrequency(&p.qpcFreq);
    p.ready = true;
}

inline void GhostProf_Shutdown(GhostGpuProf& p) { p.heap.Reset(); p.readback.Reset(); p.ready = p.active = p.valid = false; }

// Once per viewport frame, right after BeginFrame. on = this is the main view
inline void GhostProf_Begin(GhostGpuProf& p, bool on)
{
    p.active = on && p.ready;
    for (bool& m : p.marked) m = false;
    if (p.active) QueryPerformanceCounter(&p.t0);
}

inline void GhostProf_Mark(IGhostRHI* rhi, GhostGpuProf& p, int id)
{
    if (!p.active || id < 0 || id >= GP_COUNT) return;
    rhi->GetCmdList()->EndQuery(p.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, (UINT)id);
    p.marked[id] = true;
}

// Just before EndFrame
inline void GhostProf_Resolve(IGhostRHI* rhi, GhostGpuProf& p)
{
    if (!p.active) return;
    LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
    const float cpu = (float)((double)(t1.QuadPart - p.t0.QuadPart) * 1000.0 / (double)p.qpcFreq.QuadPart);
    p.cpuMs = p.cpuMs > 0.0f ? p.cpuMs + (cpu - p.cpuMs) * 0.1f : cpu;
    for (bool m : p.marked) if (!m) { p.active = false; return; }      // a mark was skipped: drop this frame
    rhi->GetCmdList()->ResolveQueryData(p.heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, GP_COUNT, p.readback.Get(), 0);
}

// Right after EndFrame (EndFrame waits for the GPU, so the data is ready)
inline void GhostProf_Collect(GhostGpuProf& p)
{
    if (!p.active) return;
    p.active = false;
    UINT64* src = nullptr;
    D3D12_RANGE rr = { 0, sizeof(UINT64) * GP_COUNT };
    if (FAILED(p.readback->Map(0, &rr, (void**)&src)) || !src) return;
    UINT64 ts[GP_COUNT];
    memcpy(ts, src, sizeof ts);
    D3D12_RANGE wr = { 0, 0 };
    p.readback->Unmap(0, &wr);
    for (int i = 1; i < GP_COUNT; i++) if (ts[i] < ts[i - 1]) return;  // not finished / garbage

    const float k = 1000.0f / (float)p.freq;
    float tot = 0.0f;
    for (int i = 1; i < GP_COUNT; i++)
    {
        const float ms = (float)(ts[i] - ts[i - 1]) * k;
        p.seg[i] = p.valid ? p.seg[i] + (ms - p.seg[i]) * 0.1f : ms;
        tot += p.seg[i];
    }
    p.total = tot;
    p.valid = true;
}