#pragma once

#include <DirectXMathSSE4.h>
#include "HelperStructs.h"
#include "..\Common\Constants.h"
#include "..\Common\DynBitSet.h"

namespace D3D12 {
    class Renderer {
    public:
        RULE_OF_ZERO_MOVE_ONLY(Renderer);
        Renderer();
        // Creates a vertex attribute buffer for the vertex array of 'count' elements.
        template <typename T>
        VertexBuffer createVertexBuffer(const uint count, const T* elements);
        // Creates an index buffer for the index array with the specified number of indices.
        IndexBuffer createIndexBuffer(const uint count, const uint* indices);
        // Creates a constant buffer for the data of the specified size (in bytes).
        ConstantBuffer createConstantBuffer(const uint size, const void* data = nullptr);
        // Creates a 2D texture according to the provided description of the base MIP image.
        // Returns the texture itself and its index within the texture pool.
        // Multi-sample textures and texture arrays are not supported.
        std::pair<Texture, uint> createTexture2D(const D3D12_SUBRESOURCE_FOOTPRINT& footprint,
                                                 const uint mipCount, const void* data = nullptr);
        // Sets materials (represented by texture indices) in shaders.
        void setMaterials(const uint size, const void* data);
        // Sets the view-projection matrix in shaders.
        void setViewProjMatrix(DirectX::FXMMATRIX viewProj);
        // Submits all pending copy commands for execution, and begins a new segment
        // of the upload buffer. As a result, the previous segment of the buffer becomes
        // available for writing. 'immediateCopy' flag ensures that all copies from the
        // current segment are also completed during this call (at the cost of blocking
        // the thread), therefore making the entire buffer free and available for writing.
        void executeCopyCommands(const bool immediateCopy = false);
        // Returns the current time of the CPU thread and the GPU queue in microseconds.
        std::pair<uint64, uint64> getTime() const;
        // Initializes the frame rendering process.
        void startFrame();
        template <uint N>
        // Draws geometry using 'N' vertex attribute buffers and 'iboCount' index buffers.
        // 'materialIndices' associates index buffers with material indices.
        // 'drawMask' indicates whether geometry (within 'ibos') should be drawn.
        void drawIndexed(const VertexBuffer (&vbos)[N],
                         const IndexBuffer* ibos, const uint iboCount,
                         const uint16* materialIndices,
                         const DynBitSet& drawMask);
        // Finalizes the frame rendering process.
        void finalizeFrame();
        // Terminates the rendering process.
        void stop();
    private:
        // Configures the rendering pipeline, including the shaders.
        void configurePipeline();
        // Copies the data of the specified size (in bytes) and alignment into the upload buffer.
        // Returns the offset into the upload buffer which corresponds to the location of the data.
        template<uint64 alignment>
        uint copyToUploadBuffer(const uint size, const void* data);
        // Reserves a contiguous chunk of memory of the specified size within the upload buffer.
        // The reservation is guaranteed to be valid only until any other member function call.
        // Returns the address of and the offset to the beginning of the chunk of the upload buffer.
        template<uint64 alignment>
        std::pair<byte*, uint> reserveChunkOfUploadBuffer(const uint size);
    private:
        ComPtr<ID3D12DeviceEx>        m_device;
        D3D12_VIEWPORT                m_viewport;
        D3D12_RECT                    m_scissorRect;
        GraphicsContext<FRAME_CNT, 1> m_graphicsContext;
        uint                          m_frameIndex;
        uint                          m_backBufferIndex;
        ComPtr<ID3D12Resource>        m_renderTargets[BUF_CNT];
        RtvPool<BUF_CNT>              m_rtvPool;
        DsvPool<FRAME_CNT>            m_dsvPool;
        CbvSrvUavPool<TEX_CNT>        m_texPool;
        ComPtr<IDXGISwapChain3>       m_swapChain;
        HANDLE                        m_swapChainWaitableObject;
        CopyContext<2, 1>             m_copyContext;
        UploadRingBuffer              m_uploadBuffer;
        ConstantBuffer                m_materialBuffer;
        /* Frame resources */
        struct {
            ComPtr<ID3D12Resource>    depthBuffer;
            ConstantBuffer            transformBuffer;
        }                             m_frameResouces[FRAME_CNT];
        /* Pipeline objects */
        ComPtr<ID3D12RootSignature>   m_graphicsRootSignature;
        ComPtr<ID3D12PipelineState>   m_graphicsPipelineState;
    };
} // namespace D3D12
