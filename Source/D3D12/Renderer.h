#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl\client.h>
#include "..\Common\Definitions.h"

namespace D3D12 {
    using Microsoft::WRL::ComPtr;

    // Direct3D renderer
    class Renderer {
    public:
        RULE_OF_ZERO_MOVE_ONLY(Renderer);
        // Constructor; takes horizontal and vertical resolution,
        // as well as window handle as input
        Renderer(const LONG resX, const LONG resY, const HWND wnd);
    private:
        // Initializes rendering pipeline
        void initPipeline();
        // Enables Direct3D debug layer
        void enableDebugLayer() const;
        // Creates WARP (software) Direct3D device
        void createWarpDevice(IDXGIFactory4* const factory);
        // Creates hardware Direct3D device
        void createHardwareDevice(IDXGIFactory4* const factory);
        // Creates command queue
        void createCommandQueue();
        // Creates swap chain
        void createSwapChain(IDXGIFactory4* const factory);
        // Creates descriptor heap
        void createDescriptorHeap();
        // Creates RTVs for each frame buffer
        void createRenderTargetViews();
        // TODO
        void loadAssets();
        /* Frame buffer count */
        static const UINT                 m_bufferCount = 2;
        /* Adapter info */
	    static const bool                 m_useWarpDevice = false;
        /* Window handle */
        HWND                              m_windowHandle;
        /* Viewport dimensions */
        LONG                              m_width;
        LONG                              m_height;
        float                             m_aspectRatio;
        /* Pipeline objects */
        D3D12_VIEWPORT                    m_viewport;
        D3D12_RECT                        m_scissorRect;
        ComPtr<IDXGISwapChain3>           m_swapChain;
        ComPtr<ID3D12Device>              m_device;
        ComPtr<ID3D12Resource>            m_renderTargets[m_bufferCount];
        ComPtr<ID3D12CommandAllocator>    m_commandAllocator;
        ComPtr<ID3D12CommandQueue>        m_commandQueue;
        ComPtr<ID3D12RootSignature>       m_rootSignature;
        ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
        ComPtr<ID3D12PipelineState>       m_pipelineState;
        ComPtr<ID3D12GraphicsCommandList> m_commandList;
        UINT                              m_rtvDescriptorSize;
        /* Application resources */
        ComPtr<ID3D12Resource>            m_vertexBuffer;
        D3D12_VERTEX_BUFFER_VIEW          m_vertexBufferView;
        /* Synchronization objects */
        UINT                              m_frameIndex;
        HANDLE                            m_fenceEvent;
        ComPtr<ID3D12Fence>               m_fence;
        UINT64                            m_fenceValue;
    };
} // namespace D3D12
