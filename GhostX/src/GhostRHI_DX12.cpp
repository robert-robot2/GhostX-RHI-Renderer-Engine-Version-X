// GhostRHI_DX12.cpp
// This is GhostRuntime.cpp, promoted into a class.
// Logic is identical — names moved from free functions to methods.

#include "../include/GhostRHI_DX12.h"
#include "../include/Ghost3D1.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <Windows.h>

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
    rtvDesc.NumDescriptors = FRAME_COUNT;
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
    m_swapChain->Present(1, 0);

    FlushGPU();
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

// ============================================================
//  Resize
// ============================================================

void GhostRHI_DX12::Resize(unsigned int width, unsigned int height)
{
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
    rtv.ptr += (SIZE_T)m_frameIndex * m_rtvDescSize;
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

    for (int i = 0; i < FRAME_COUNT; i++)
    {
        m_renderTargets[i].Reset();
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
        m_device->CreateRenderTargetView(
            m_renderTargets[i].Get(), nullptr, handle);
        handle.ptr += m_rtvDescSize;
    }
}