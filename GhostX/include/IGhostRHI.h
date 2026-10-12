// IGhostRHI.h
#pragma once
#include <Windows.h>
#include <d3d12.h>
#include "../include/Ghost3D1.h"   // GhostResult, GhostColor
#include <functional>
// ---- Backend selector ----
enum class RHIType
{
    DX12,
    DX11,     // not yet implemented
    Vulkan,   // not yet implemented
    OpenGL,   // not yet implemented
};

constexpr int GHOST_MAX_SURFACES = 5;     // surface 0 = main view, 1..4 = camera windows

// ---- The interface ----
// Nobody above this layer ever sees a ComPtr, a VkDevice, or a DXGI type.
// GhostPipeline is the one exception: it is DX12-specific today and will
// be abstracted in a later phase. For now the interface hands back raw
// D3D12 primitives only through GetDevice / GetCmdList so Pipeline can
// keep compiling unchanged.

class IGhostRHI
{
public:
    virtual ~IGhostRHI() = default;

    // ---- Lifecycle ----
    virtual GhostResult Init(HWND hwnd, unsigned int width, unsigned int height) = 0;
    virtual void        Shutdown() = 0;

    // ---- Per-frame ----
    // BeginFrame resets the allocator/list, transitions to RENDER_TARGET,
    // clears color + depth, and sets viewport/scissor.
    // dsv is the pipeline's depth stencil view — passed in because the
    // depth buffer lives in GhostPipeline today. When GhostPipeline is
    // also abstracted this parameter goes away.
    virtual void BeginFrame(const GhostColor* clearColor,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv) = 0;

    // EndFrame transitions to PRESENT, executes, presents, and waits.
    virtual void EndFrame() = 0;

    // ---- Resize ----
    virtual void Resize(unsigned int width, unsigned int height) = 0;

    // ---- Config ----
    virtual void SetClearColor(float r, float g, float b) = 0;

    // ---- Access for GhostPipeline (DX12 only today) ----
    // These return nullptr on non-DX12 backends until Pipeline is abstracted.
    virtual ID3D12Device* GetDevice() = 0;
    virtual ID3D12GraphicsCommandList* GetCmdList() = 0;
    virtual D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() = 0;
    // ---- One-shot GPU work (texture uploads etc.) ----
// Records into a private list, submits, and waits for the GPU. Call between frames only.
    virtual GhostResult ExecuteOneShot(
        const std::function<void(ID3D12GraphicsCommandList*)>& record) = 0;
    // ---- Surfaces (one swap chain per viewport window) ----
    virtual int          CreateSurface(HWND hwnd, unsigned int width, unsigned int height) = 0;   // id >= 1, or -1
    virtual void         DestroySurface(int id) = 0;
    virtual GhostResult  RebindSurface(int id, HWND hwnd, unsigned int width, unsigned int height) = 0;
    virtual void         ResizeSurface(int id, unsigned int width, unsigned int height) = 0;
    // Picks the surface that BeginFrame / EndFrame / GetRTV / Resize act on
    virtual bool         SetCurrentSurface(int id) = 0;
    virtual int          GetCurrentSurface() = 0;
    virtual unsigned int GetSurfaceWidth() = 0;
    virtual unsigned int GetSurfaceHeight() = 0;
};

// ---- Factory ----
IGhostRHI* CreateRHI(RHIType type);