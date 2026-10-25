// GhostRHI_DX12.h
#pragma once
#include "IGhostRHI.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

class GhostRHI_DX12 : public IGhostRHI
{
public:
    GhostRHI_DX12() = default;
    ~GhostRHI_DX12() override;

    // IGhostRHI
    GhostResult Init(HWND hwnd, unsigned int width, unsigned int height) override;
    void        Shutdown() override;

    void BeginFrame(const GhostColor* clearColor,
        D3D12_CPU_DESCRIPTOR_HANDLE dsv) override;
    void EndFrame() override;

    void Resize(unsigned int width, unsigned int height) override;
    void SetClearColor(float r, float g, float b)              override;

    ID3D12Device* GetDevice()  override;
    ID3D12GraphicsCommandList* GetCmdList() override;
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV()     override;
    GhostResult ExecuteOneShot(
        const std::function<void(ID3D12GraphicsCommandList*)>& record) override;
    int          CreateSurface(HWND hwnd, unsigned int width, unsigned int height) override;
    void         DestroySurface(int id) override;
    GhostResult  RebindSurface(int id, HWND hwnd, unsigned int width, unsigned int height) override;
    void         ResizeSurface(int id, unsigned int width, unsigned int height) override;
    bool         SetCurrentSurface(int id) override;
    int          GetCurrentSurface() override { return m_cur; }
    unsigned int GetSurfaceWidth() override { return m_width; }
    unsigned int GetSurfaceHeight() override { return m_height; }



private:
    void FlushGPU();
    void RebuildRTVs();
    void StashCurrent();
    void LoadSlot(int id);
    bool MakeSwapChain(HWND hwnd, unsigned int w, unsigned int h, ComPtr<IDXGISwapChain3>& out);

    HWND         m_hwnd = nullptr;
    unsigned int m_width = 0;
    unsigned int m_height = 0;

    static constexpr int FRAME_COUNT = 2;
    struct SurfaceSlot
    {
        bool         alive = false;
        HWND         hwnd = nullptr;
        unsigned int width = 0, height = 0, frameIndex = 0;
        ComPtr<IDXGISwapChain3> swapChain;
        ComPtr<ID3D12Resource>  renderTargets[FRAME_COUNT];
    };
    SurfaceSlot m_surfaces[GHOST_MAX_SURFACES];
    int         m_cur = 0;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_cmdQueue;
    ComPtr<IDXGISwapChain3>           m_swapChain;
    ComPtr<ID3D12Resource>            m_renderTargets[FRAME_COUNT];
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    unsigned int                      m_rtvDescSize = 0;
    ComPtr<ID3D12CommandAllocator>    m_cmdAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_cmdList;
    ComPtr<ID3D12Fence>               m_fence;
    unsigned long long                m_fenceValue = 0;
    HANDLE                            m_fenceEvent = nullptr;
    unsigned int                      m_frameIndex = 0;

    GhostColor m_clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
    ComPtr<ID3D12CommandAllocator>    m_uploadAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_uploadList;
};