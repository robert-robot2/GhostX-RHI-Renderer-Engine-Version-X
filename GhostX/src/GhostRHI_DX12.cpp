// GhostRHI_DX12.cpp
// This is GhostRuntime.cpp, promoted into a class.
// Logic is identical — names moved from free functions to methods.

#include "../include/GhostRHI_DX12.h"
#include "../include/Ghost3D1.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <Windows.h>
#include <utility>
// ============================================================
//  Factory
// ============================================================

IGhostRHI* CreateRHI(RHIType type)
{
    switch (type)
    {
    case RHIType::DX12:   return new GhostRHI_DX12();
    default:
        // Backends not yet implemented — callers check for nullptr
        return nullptr;
    }
}

// ============================================================
//  Destructor
// ============================================================

GhostRHI_DX12::~GhostRHI_DX12()
{
    // Shutdown may have already been called explicitly;
    // guard so the fence handle isn't double-closed.
    if (m_fenceEvent)
        Shutdown();
}

// ============================================================
//  Init  (was GhostBoot)
// ============================================================

GhostResult GhostRHI_DX12::Init(HWND hwnd, unsigned int width, unsigned int height)
{
    m_hwnd = hwnd;
    m_width = width;
    m_height = height;

    // ---- Device ----
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0,
        IID_PPV_ARGS(&m_device))))
        return GHOST_FAIL;

    // ---- Command Queue ----
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_cmdQueue));

    // ---- Swap Chain ----
    ComPtr<IDXGIFactory4> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = FRAME_COUNT;
    scd.Width = width;
    scd.Height = height;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    factory->CreateSwapChainForHwnd(
        m_cmdQueue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
    sc1.As(&m_swapChain);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    // ---- RTV Heap ----
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = FRAME_COUNT * GHOST_MAX_SURFACES;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_rtvHeap));
    m_rtvDescSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // ---- Command Allocator + List ----
    m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAllocator));
    m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_cmdAllocator.Get(), nullptr, IID_PPV_ARGS(&m_cmdList));
    m_cmdList->Close();
    // ---- One-shot upload allocator + list ----
    m_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_uploadAllocator));
    m_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_uploadAllocator.Get(), nullptr, IID_PPV_ARGS(&m_uploadList));
    m_uploadList->Close();
    // ---- Fence ----
    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    // ---- Sync to actual client rect ----
    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    m_swapChain->ResizeBuffers(
        FRAME_COUNT, m_width, m_height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    RebuildRTVs();
    m_surfaces[0].alive = true;
    m_cur = 0;
    return GHOST_OK;
}



// ============================================================
//  Shutdown  (was GhostShutdown)
// ============================================================

void GhostRHI_DX12::Shutdown()
{
    if (!m_fenceEvent) return;

    FlushGPU();

    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
    // ComPtrs release D3D objects when they go out of scope / are reset
}

// ============================================================
//  BeginFrame  (was GhostClear)
// ============================================================

void GhostRHI_DX12::BeginFrame(const GhostColor* color,
    D3D12_CPU_DESCRIPTOR_HANDLE dsv)
{
    m_cmdAllocator->Reset();
    m_cmdList->Reset(m_cmdAllocator.Get(), nullptr);

    // Transition back-buffer → RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = GetRTV();
    m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    D3D12_VIEWPORT vp = {};
    vp.Width = (float)m_width;
    vp.Height = (float)m_height;
    vp.MaxDepth = 1.0f;

    D3D12_RECT sc = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_cmdList->RSSetViewports(1, &vp);
    m_cmdList->RSSetScissorRects(1, &sc);

    float c[4] = { color->r, color->g, color->b, color->a };
    m_cmdList->ClearRenderTargetView(rtv, c, 0, nullptr);
    m_cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

// ============================================================
//  EndFrame  (was GhostPresent)
// ============================================================

void GhostRHI_DX12::EndFrame()
{
    // Transition back-buffer → PRESENT
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_cmdList->ResourceBarrier(1, &barrier);
    m_cmdList->Close();

    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, lists);
    m_swapChain->Present(m_cur == 0 ? 1 : 0, 0);

    FlushGPU();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// ============================================================
//  Resize
// ============================================================

void GhostRHI_DX12::Resize(unsigned int width, unsigned int height)
{
    if (!m_swapChain) return;
    if (width == 0 || height == 0) return;

    FlushGPU();

    for (int i = 0; i < FRAME_COUNT; i++)
        m_renderTargets[i].Reset();

    m_swapChain->ResizeBuffers(
        FRAME_COUNT, width, height,
        DXGI_FORMAT_R8G8B8A8_UNORM, 0);

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    RebuildRTVs();
}

// ============================================================
//  SetClearColor
// ============================================================

void GhostRHI_DX12::SetClearColor(float r, float g, float b)
{
    m_clearColor = { r, g, b, 1.0f };
}

// ============================================================
//  Accessors
// ============================================================

ID3D12Device* GhostRHI_DX12::GetDevice()
{
    return m_device.Get();
}

ID3D12GraphicsCommandList* GhostRHI_DX12::GetCmdList()
{
    return m_cmdList.Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE GhostRHI_DX12::GetRTV()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)(m_cur * FRAME_COUNT + m_frameIndex) * m_rtvDescSize;
    return rtv;
}
GhostResult GhostRHI_DX12::ExecuteOneShot(
    const std::function<void(ID3D12GraphicsCommandList*)>& record)
{
    if (!m_uploadList) return GHOST_FAIL;
    if (FAILED(m_uploadAllocator->Reset())) return GHOST_FAIL;
    if (FAILED(m_uploadList->Reset(m_uploadAllocator.Get(), nullptr))) return GHOST_FAIL;

    record(m_uploadList.Get());

    if (FAILED(m_uploadList->Close())) return GHOST_FAIL;
    ID3D12CommandList* lists[] = { m_uploadList.Get() };
    m_cmdQueue->ExecuteCommandLists(1, lists);
    FlushGPU();
    return GHOST_OK;
}
// ============================================================
//  Private helpers
// ============================================================

void GhostRHI_DX12::FlushGPU()
{
    const unsigned long long fv = ++m_fenceValue;
    m_cmdQueue->Signal(m_fence.Get(), fv);
    if (m_fence->GetCompletedValue() < fv)
    {
        m_fence->SetEventOnCompletion(fv, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void GhostRHI_DX12::RebuildRTVs()
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += (SIZE_T)m_cur * FRAME_COUNT * m_rtvDescSize;

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        m_renderTargets[i].Reset();
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(
            m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescSize;
    }
}

// ============================================================
//  Surfaces
// ============================================================
void GhostRHI_DX12::StashCurrent()
{
    SurfaceSlot& s = m_surfaces[m_cur];
    s.hwnd = m_hwnd; s.width = m_width; s.height = m_height; s.frameIndex = m_frameIndex;
    s.swapChain = std::move(m_swapChain);
    for (int i = 0; i < FRAME_COUNT; i++) s.renderTargets[i] = std::move(m_renderTargets[i]);
}

void GhostRHI_DX12::LoadSlot(int id)
{
    SurfaceSlot& s = m_surfaces[id];
    m_cur = id;
    m_hwnd = s.hwnd; m_width = s.width; m_height = s.height; m_frameIndex = s.frameIndex;
    m_swapChain = std::move(s.swapChain);
    for (int i = 0; i < FRAME_COUNT; i++) m_renderTargets[i] = std::move(s.renderTargets[i]);
}

bool GhostRHI_DX12::SetCurrentSurface(int id)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m_surfaces[id].alive) return false;
    if (id == m_cur) return true;
    if (!m_surfaces[id].swapChain) return false;      // a failed rebind left it without a swap chain
    StashCurrent();
    LoadSlot(id);
    return true;
}

bool GhostRHI_DX12::MakeSwapChain(HWND hwnd, unsigned int w, unsigned int h,
    ComPtr<IDXGISwapChain3>& out)
{
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = FRAME_COUNT;
    scd.Width = w;
    scd.Height = h;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> sc1;
    if (FAILED(factory->CreateSwapChainForHwnd(m_cmdQueue.Get(), hwnd, &scd, nullptr, nullptr, &sc1)))
        return false;
    return SUCCEEDED(sc1.As(&out));
}

int GhostRHI_DX12::CreateSurface(HWND hwnd, unsigned int width, unsigned int height)
{
    int id = -1;
    for (int i = 1; i < GHOST_MAX_SURFACES; i++)
        if (!m_surfaces[i].alive) { id = i; break; }
    if (id < 0) return -1;
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    ComPtr<IDXGISwapChain3> sc;
    if (!MakeSwapChain(hwnd, width, height, sc)) return -1;

    SurfaceSlot& s = m_surfaces[id];
    s.alive = true;
    s.hwnd = hwnd;
    s.width = width;
    s.height = height;
    s.frameIndex = sc->GetCurrentBackBufferIndex();
    s.swapChain = sc;

    int prev = m_cur;
    SetCurrentSurface(id);
    RebuildRTVs();
    SetCurrentSurface(prev);
    return id;
}

void GhostRHI_DX12::DestroySurface(int id)
{
    if (id <= 0 || id >= GHOST_MAX_SURFACES || !m_surfaces[id].alive) return;
    FlushGPU();
    if (m_cur == id) SetCurrentSurface(0);

    SurfaceSlot& s = m_surfaces[id];
    for (int i = 0; i < FRAME_COUNT; i++) s.renderTargets[i].Reset();
    s.swapChain.Reset();
    s.alive = false;
    s.hwnd = nullptr;
    s.width = s.height = 0;
}

// The window behind a surface was recreated by WinForms: give the surface a swap chain for the new one
GhostResult GhostRHI_DX12::RebindSurface(int id, HWND hwnd, unsigned int width, unsigned int height)
{
    if (id < 0 || id >= GHOST_MAX_SURFACES || !m_surfaces[id].alive) return GHOST_FAIL;
    if (width == 0) width = 1;
    if (height == 0) height = 1;

    FlushGPU();
    int prev = m_cur;
    SetCurrentSurface(id);

    for (int i = 0; i < FRAME_COUNT; i++) m_renderTargets[i].Reset();
    m_swapChain.Reset();

    GhostResult r = GHOST_FAIL;
    if (MakeSwapChain(hwnd, width, height, m_swapChain))
    {
        m_hwnd = hwnd;
        m_width = width;
        m_height = height;
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
        RebuildRTVs();
        r = GHOST_OK;
    }
    else
    {
        m_width = m_height = 0;               // the engine skips a surface with no size
    }
    SetCurrentSurface(prev);
    return r;
}

void GhostRHI_DX12::ResizeSurface(int id, unsigned int width, unsigned int height)
{
    int prev = m_cur;
    if (!SetCurrentSurface(id)) return;
    Resize(width, height);
    SetCurrentSurface(prev);
}