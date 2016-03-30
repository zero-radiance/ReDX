#include <d3dcompiler.h>
#include <d3dx12.h>
#include <memory>
#include <tuple>
#include "HelperStructs.hpp"
#include "Renderer.hpp"
#include "..\Common\Math.h"
#include "..\UI\Window.h"

using namespace D3D12;
using namespace DirectX;

static inline auto createWarpDevice(IDXGIFactory4* const factory)
-> ComPtr<ID3D12DeviceEx> {
    ComPtr<IDXGIAdapter> adapter;
    CHECK_CALL(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),
               "Failed to create a WARP adapter.");
    ComPtr<ID3D12DeviceEx> device;
    CHECK_CALL(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)),
               "Failed to create a Direct3D device.");
    return device;
}

static inline auto createHardwareDevice(IDXGIFactory4* const factory)
-> ComPtr<ID3D12DeviceEx> {
    for (uint adapterIndex = 0; ; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        if (DXGI_ERROR_NOT_FOUND == factory->EnumAdapters1(adapterIndex, &adapter)) {
            // No more adapters to enumerate.
            printError("Direct3D 12 device not found.");
            TERMINATE();
        }
        // Check whether the adapter supports Direct3D 12.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                        _uuidof(ID3D12Device), nullptr))) {
            // It does -> create a Direct3D device.
            ComPtr<ID3D12DeviceEx> device;
            CHECK_CALL(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                         IID_PPV_ARGS(&device)),
                       "Failed to create a Direct3D device.");
            return device;
        }
    }
}

Renderer::Renderer() {
    const long resX = Window::width();
    const long resY = Window::height();
    // Configure the viewport.
    m_viewport = D3D12_VIEWPORT{
        /* TopLeftX */ 0.f,
        /* TopLeftY */ 0.f,
        /* Width */    static_cast<float>(resX),
        /* Height */   static_cast<float>(resY),
        /* MinDepth */ 0.f,
        /* MaxDepth */ 1.f
    };
    // Configure the scissor rectangle used for clipping.
    m_scissorRect = D3D12_RECT{
        /* left */   0,
        /* top */    0, 
        /* right */  resX,
        /* bottom */ resY
    };
    // Enable the Direct3D debug layer.
    #ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> debugController;
        CHECK_CALL(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)),
                   "Failed to initialize the D3D debug layer.");
        debugController->EnableDebugLayer();
    }
    #endif
    // Create a DirectX Graphics Infrastructure (DXGI) 1.4 factory.
    // IDXGIFactory4 inherits from IDXGIFactory1 (4 -> 3 -> 2 -> 1).
    ComPtr<IDXGIFactory4> factory;
    CHECK_CALL(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),
               "Failed to create a DXGI 1.4 factory.");
    // Disable transitions from the windowed to the fullscreen mode.
    CHECK_CALL(factory->MakeWindowAssociation(Window::handle(), DXGI_MWA_NO_ALT_ENTER),
               "Failed to disable fullscreen transitions.");
    // Create a Direct3D device that represents the display adapter.
    #pragma warning(suppress: 4127)
    if (USE_WARP_DEVICE) {
        // Use software rendering.
        m_device = createWarpDevice(factory.Get());
    } else {
        // Use hardware acceleration.
        m_device = createHardwareDevice(factory.Get());
    }
    // Create command contexts.
    m_device->createCommandContext(&m_copyContext);
    m_device->createCommandContext(&m_graphicsContext);
    // Create descriptor pools.
    m_device->createDescriptorPool(&m_rtvPool);
    m_device->createDescriptorPool(&m_dsvPool);
    m_device->createDescriptorPool(&m_texPool);
    // Create a buffer swap chain.
    {
        // Fill out the multi-sampling parameters.
        const DXGI_SAMPLE_DESC sampleDesc = {
            /* Count */   1,    // No multi-sampling
            /* Quality */ 0     // Default sampler
        };
        // Fill out the swap chain description.
        const DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {
            /* Width */       width(m_scissorRect),
            /* Height */      height(m_scissorRect),
            /* Format */      RTV_FORMAT,
            /* Stereo */      0,
            /* SampleDesc */  sampleDesc,
            /* BufferUsage */ DXGI_USAGE_RENDER_TARGET_OUTPUT,
            /* BufferCount */ BUF_CNT,
            /* Scaling */     DXGI_SCALING_NONE,
            /* SwapEffect */  DXGI_SWAP_EFFECT_FLIP_DISCARD,
            /* AlphaMode */   DXGI_ALPHA_MODE_UNSPECIFIED,
            /* Flags */       DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT
        };
        // Create a swap chain for the window.
        m_swapChain = m_graphicsContext.createSwapChain(factory.Get(), Window::handle(),
                                                        swapChainDesc);
        // Set the maximal rendering queue depth.
        CHECK_CALL(m_swapChain->SetMaximumFrameLatency(FRAME_CNT),
                   "Failed to set the maximal frame latency of the swap chain.");
        // Retrieve the object used to wait for the swap chain.
        m_swapChainWaitableObject = m_swapChain->GetFrameLatencyWaitableObject();
        // Block the thread until the swap chain is ready accept a new frame.
        WaitForSingleObject(m_swapChainWaitableObject, INFINITE);
        // Update the index of the frame buffer used for rendering.
        m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    }
    // Create render target views (RTVs).
    {
        // Create an RTV for each frame buffer.
        for (uint bufferIdx = 0; bufferIdx < BUF_CNT; ++bufferIdx) {
            CHECK_CALL(m_swapChain->GetBuffer(bufferIdx, IID_PPV_ARGS(&m_renderTargets[bufferIdx])),
                       "Failed to acquire a swap chain buffer.");
            m_device->CreateRenderTargetView(m_renderTargets[bufferIdx].Get(), nullptr,
                                             m_rtvPool.getCpuHandle(m_rtvPool.size++));
        }
    }
    // Create a depth-stencil buffer.
    {
        const DXGI_SAMPLE_DESC sampleDesc = {
            /* Count */   1,            // No multi-sampling
            /* Quality */ 0             // Default sampler
        };
        const D3D12_RESOURCE_DESC bufferDesc = {
            /* Dimension */        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            /* Alignment */        0,   // Automatic
            /* Width */            width(m_scissorRect),
            /* Height */           height(m_scissorRect),
            /* DepthOrArraySize */ 1,
            /* MipLevels */        0,   // Automatic
            /* Format */           DSV_FORMAT,
            /* SampleDesc */       sampleDesc,
            /* Layout */           D3D12_TEXTURE_LAYOUT_UNKNOWN,
            /* Flags */            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
        };
        const CD3DX12_CLEAR_VALUE clearValue{
            /* Format */  DSV_FORMAT,
            /* Depth */   0.f,
            /* Stencil */ 0
        };
        const CD3DX12_HEAP_PROPERTIES heapProperties{D3D12_HEAP_TYPE_DEFAULT};
        CHECK_CALL(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                     &bufferDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                     &clearValue, IID_PPV_ARGS(&m_depthBuffer)),
                   "Failed to allocate a depth buffer.");
        const D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {
            /* Format */        DSV_FORMAT,
            /* ViewDimension */ D3D12_DSV_DIMENSION_TEXTURE2D,
            /* Flags */         D3D12_DSV_FLAG_NONE,
            /* Texture2D */     {}
        };
        m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc,
                                         m_dsvPool.getCpuHandle(m_dsvPool.size++));
    }
    // Create a persistently mapped buffer on the upload heap.
    {
        m_uploadBuffer.capacity   = UPLOAD_BUF_SIZE;
        // Allocate the buffer on the upload heap.
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_UPLOAD};
        const auto bufferDesc     = CD3DX12_RESOURCE_DESC::Buffer(m_uploadBuffer.capacity);
        CHECK_CALL(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, 
                                                     &bufferDesc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                     IID_PPV_ARGS(&m_uploadBuffer.resource)),
                   "Failed to allocate an upload buffer.");
        // Note: we don't intend to read from this resource on the CPU.
        constexpr D3D12_RANGE emptyReadRange{0, 0};
        // Map the buffer to a range in the CPU virtual address space.
        CHECK_CALL(m_uploadBuffer.resource->Map(0, &emptyReadRange,
                                                reinterpret_cast<void**>(&m_uploadBuffer.begin)),
                   "Failed to map the upload buffer.");
    }
    // Set up the rendering pipeline.
    configurePipeline();
}

struct Shader {
    size_t                  size;
    std::unique_ptr<byte[]> bytecode;
};

// Loads the shader bytecode from the file.
static inline auto loadShaderBytecode(const char* const pathAndFileName)
-> Shader {
    FILE* file;
    if (fopen_s(&file, pathAndFileName, "rb")) {
        printError("Shader file %s not found.", pathAndFileName);
        TERMINATE();
    }
    Shader shader;
    // Get the file size in bytes.
    fseek(file, 0, SEEK_END);
    shader.size = ftell(file);
    rewind(file);
    // Read the file.
    shader.bytecode = std::make_unique<byte[]>(shader.size);
    fread(shader.bytecode.get(), 1, shader.size, file);
    // Close the file and return the bytecode.
    fclose(file);
    return shader;
}

void Renderer::configurePipeline() {
    // Create a graphics root signature.
    {
        CD3DX12_ROOT_PARAMETER vertexCBV;
        vertexCBV.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        // Fill out the root signature description.
        const auto rootSignDesc = D3D12_ROOT_SIGNATURE_DESC{1, &vertexCBV, 0, nullptr,
                                  D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
        // Serialize a root signature from the description.
        ComPtr<ID3DBlob> signature, error;
        CHECK_CALL(D3D12SerializeRootSignature(&rootSignDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                               &signature, &error),
                   "Failed to serialize a root signature.");
        // Create a root signature layout using the serialized signature.
        CHECK_CALL(m_device->CreateRootSignature(m_device->nodeMask,
                                                 signature->GetBufferPointer(),
                                                 signature->GetBufferSize(),
                                                 IID_PPV_ARGS(&m_graphicsRootSignature)),
                   "Failed to create a graphics root signature.");
    }
    // Import the vertex and pixel shaders.
    const Shader vs = loadShaderBytecode("Shaders\\DrawVS.cso");
    const Shader ps = loadShaderBytecode("Shaders\\DrawPS.cso");
    // Configure the way depth and stencil tests affect stencil values.
    const D3D12_DEPTH_STENCILOP_DESC depthStencilOpDesc = {
        /* StencilFailOp */      D3D12_STENCIL_OP_KEEP,
        /* StencilDepthFailOp */ D3D12_STENCIL_OP_KEEP,
        /* StencilPassOp  */     D3D12_STENCIL_OP_KEEP,
        /* StencilFunc */        D3D12_COMPARISON_FUNC_ALWAYS
    };
    // Fill out the depth stencil description.
    const D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {
        /* DepthEnable */      TRUE,
        /* DepthWriteMask */   D3D12_DEPTH_WRITE_MASK_ALL,
        /* DepthFunc */        D3D12_COMPARISON_FUNC_GREATER,
        /* StencilEnable */    FALSE,
        /* StencilReadMask */  D3D12_DEFAULT_STENCIL_READ_MASK,
        /* StencilWriteMask */ D3D12_DEFAULT_STENCIL_WRITE_MASK,
        /* FrontFace */        depthStencilOpDesc,
        /* BackFace */         depthStencilOpDesc
    };
    // Define the vertex input layout.
    const D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {
            /* SemanticName */         "POSITION",
            /* SemanticIndex */        0,
            /* Format */               DXGI_FORMAT_R32G32B32_FLOAT,
            /* InputSlot */            0,
            /* AlignedByteOffset */    0,
            /* InputSlotClass */       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            /* InstanceDataStepRate */ 0
        },
        {
            /* SemanticName */         "NORMAL",
            /* SemanticIndex */        0,
            /* Format */               DXGI_FORMAT_R32G32B32_FLOAT,
            /* InputSlot */            1,
            /* AlignedByteOffset */    0,
            /* InputSlotClass */       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            /* InstanceDataStepRate */ 0
        }
    };
    const D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {
        /* pInputElementDescs */ inputElementDescs,
        /* NumElements */        _countof(inputElementDescs)
    };
    // Fill out the multi-sampling parameters.
    const DXGI_SAMPLE_DESC sampleDesc = {
        /* Count */   1,    // No multi-sampling
        /* Quality */ 0     // Default sampler
    };
    // Configure the rasterizer state.
    const D3D12_RASTERIZER_DESC rasterizerDesc = {
        /* FillMode */              D3D12_FILL_MODE_SOLID,
        /* CullMode */              D3D12_CULL_MODE_BACK,
        /* FrontCounterClockwise */ FALSE,
        /* DepthBias */             D3D12_DEFAULT_DEPTH_BIAS,
        /* DepthBiasClamp */        D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        /* SlopeScaledDepthBias */  D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        /* DepthClipEnable */       TRUE,
        /* MultisampleEnable */     FALSE,
        /* AntialiasedLineEnable */ FALSE,
        /* ForcedSampleCount */     0,
        /* ConservativeRaster */    D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    };
    // Fill out the pipeline state object description.
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDesc = {
        /* pRootSignature */        m_graphicsRootSignature.Get(),
        /* VS */                    D3D12_SHADER_BYTECODE{
            /* pShaderBytecode */       vs.bytecode.get(),
            /* BytecodeLength  */       vs.size
                                    },
        /* PS */                    D3D12_SHADER_BYTECODE{
            /* pShaderBytecode */       ps.bytecode.get(),
            /* BytecodeLength  */       ps.size
                                    },
        /* DS, HS, GS */            {}, {}, {},
        /* StreamOutput */          D3D12_STREAM_OUTPUT_DESC{},
        /* BlendState */            CD3DX12_BLEND_DESC{D3D12_DEFAULT},
        /* SampleMask */            UINT_MAX,
        /* RasterizerState */       rasterizerDesc,
        /* DepthStencilState */     depthStencilDesc,
        /* InputLayout */           inputLayoutDesc,
        /* IBStripCutValue */       D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
        /* PrimitiveTopologyType */ D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        /* NumRenderTargets */      1,
        /* RTVFormats[8] */         {RTV_FORMAT},
        /* DSVFormat */             DSV_FORMAT,
        /* SampleDesc */            sampleDesc,
        /* NodeMask */              m_device->nodeMask,
        /* CachedPSO */             D3D12_CACHED_PIPELINE_STATE{},
        /* Flags */                 D3D12_PIPELINE_STATE_FLAG_NONE
    };
    // Create the initial graphics pipeline state.
    CHECK_CALL(m_device->CreateGraphicsPipelineState(&pipelineStateDesc,
                                                     IID_PPV_ARGS(&m_graphicsPipelineState)),
               "Failed to create a graphics pipeline state object.");
    // Set the command list states.
    m_copyContext.resetCommandList(0, nullptr);
    m_graphicsContext.resetCommandList(0, m_graphicsPipelineState.Get());
    // Create a constant buffer.
    m_constantBuffer = createConstantBuffer(sizeof(XMMATRIX));
}

IndexBuffer Renderer::createIndexBuffer(const uint count, const uint* const indices) {
    assert(indices && count >= 3);
    IndexBuffer buffer;
    const uint size = count * sizeof(uint);
    // Allocate the buffer on the default heap.
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT};
    const auto bufferDesc     = CD3DX12_RESOURCE_DESC::Buffer(size);
    CHECK_CALL(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                 &bufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr, IID_PPV_ARGS(&buffer.resource)),
               "Failed to allocate an index buffer.");
    // Transition the buffer state for the graphics/compute command queue type class.
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(buffer.resource.Get(),
                                                   D3D12_RESOURCE_STATE_COMMON,
                                                   D3D12_RESOURCE_STATE_INDEX_BUFFER);
    // Max. alignment requirement for indices is 4 bytes.
    constexpr uint64 alignment = 4;
    // Copy indices into the upload buffer.
    const     uint64 offset    = copyToUploadBuffer<alignment>(size, indices);
    // Copy the data from the upload buffer into the video memory buffer.
    m_copyContext.commandList(0)->CopyBufferRegion(buffer.resource.Get(), 0,
                                                   m_uploadBuffer.resource.Get(), offset,
                                                   size);
    // Initialize the index buffer view.
    buffer.view.BufferLocation = buffer.resource->GetGPUVirtualAddress(),
    buffer.view.SizeInBytes    = size;
    buffer.view.Format         = DXGI_FORMAT_R32_UINT;
    return buffer;
}

ConstantBuffer Renderer::createConstantBuffer(const uint size, const void* const data) {
    assert(!data || size >= 4);
    ConstantBuffer buffer;
    // Allocate the buffer on the default heap.
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT};
    const auto bufferDesc     = CD3DX12_RESOURCE_DESC::Buffer(size);
    CHECK_CALL(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                 &bufferDesc, D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr, IID_PPV_ARGS(&buffer.resource)),
               "Failed to allocate a constant buffer.");
    // Transition the buffer state for the graphics/compute command queue type class.
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(buffer.resource.Get(),
                                                   D3D12_RESOURCE_STATE_COMMON,
                                                   D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    m_graphicsContext.commandList(0)->ResourceBarrier(1, &barrier);
    if (data) {
        constexpr uint64 alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        // Copy the data into the upload buffer.
        const     uint64 offset    = copyToUploadBuffer<alignment>(size, data);
        // Copy the data from the upload buffer into the video memory buffer.
        m_copyContext.commandList(0)->CopyBufferRegion(buffer.resource.Get(), 0,
                                                       m_uploadBuffer.resource.Get(), offset,
                                                       size);
    }
    // Initialize the constant buffer view.
    buffer.view = buffer.resource->GetGPUVirtualAddress();
    return buffer;
}

Texture Renderer::createTexture(const D3D12_RESOURCE_DESC& desc, const uint size,
                                const void* const data) {

    assert(!data || size >= 4);
    Texture texture;
    // Allocate the texture on the default heap.
    const auto heapProperties = CD3DX12_HEAP_PROPERTIES{D3D12_HEAP_TYPE_DEFAULT};
    CHECK_CALL(m_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
                                                 &desc, D3D12_RESOURCE_STATE_COMMON,
                                                 nullptr, IID_PPV_ARGS(&texture.resource)),
               "Failed to allocate a texture.");
    // Transition the buffer state for the graphics/compute command queue type class.
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(),
                                                   D3D12_RESOURCE_STATE_COMMON,
                                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_graphicsContext.commandList(0)->ResourceBarrier(1, &barrier);
    if (data) {
        constexpr uint64 alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
        // Copy the data into the upload buffer.
        const     uint64 offset    = copyToUploadBuffer<alignment>(size, data);
        // Copy the data from the upload buffer into the video memory texture.
        const D3D12_SUBRESOURCE_FOOTPRINT footprint = {
            /* Format */   desc.Format,
            /* Width */    static_cast<uint>(desc.Width),
            /* Height */   desc.Height,
            /* Depth */    desc.DepthOrArraySize,
            /* RowPitch */ size / desc.Height
        };
        assert(0 == footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        const CD3DX12_TEXTURE_COPY_LOCATION src{m_uploadBuffer.resource.Get(), {offset, footprint}};
        const CD3DX12_TEXTURE_COPY_LOCATION dst{texture.resource.Get(), 0};
        m_copyContext.commandList(0)->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    // Describe the shader resource view.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format                        = desc.Format;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip     = 0;
    srvDesc.Texture2D.MipLevels           = 1;
    srvDesc.Texture2D.PlaneSlice          = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.f;
    // Initialize the shader resource view.
    texture.view = m_texPool.getCpuHandle(m_texPool.size++);
    m_device->CreateShaderResourceView(texture.resource.Get(), &srvDesc, texture.view);
    return texture;
}

void Renderer::setViewProjMatrix(FXMMATRIX viewProjMat) {
    const XMMATRIX tViewProj = XMMatrixTranspose(viewProjMat);
    constexpr uint64 alignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    // Copy the data into the upload buffer.
    const     uint64 offset    = copyToUploadBuffer<alignment>(sizeof(tViewProj), &tViewProj);
    // Copy the data from the upload buffer into the video memory buffer.
    m_copyContext.commandList(0)->CopyBufferRegion(m_constantBuffer.resource.Get(), 0,
                                                   m_uploadBuffer.resource.Get(), offset,
                                                   sizeof(tViewProj));
}

void D3D12::Renderer::executeCopyCommands(const bool immediateCopy) {
    // Finalize and execute the command list.
    ID3D12Fence* insertedFence;
    uint64       insertedValue;
    std::tie(insertedFence, insertedValue) = m_copyContext.executeCommandList(0);
    // Ensure synchronization between the graphics and the copy command queues.
    m_graphicsContext.syncCommandQueue(insertedFence, insertedValue);
    if (immediateCopy) {
        printWarning("Immediate copy requested. Thread stall imminent.");
        m_copyContext.syncThread(insertedValue);
    } else {
        // For single- and double-buffered copy contexts, resetCommandAllocator() will
        // take care of waiting until the previous copy command list has completed execution.
        static_assert(m_copyContext.bufferCount <= 2, "Unsupported copy context buffering mode.");
    }
    // Reset the command list allocator.
    m_copyContext.resetCommandAllocator();
    // Reset the command list to its initial state.
    m_copyContext.resetCommandList(0, nullptr);
    // Begin a new segment of the upload buffer.
    m_uploadBuffer.prevSegStart = immediateCopy ? UINT_MAX : m_uploadBuffer.currSegStart;
    m_uploadBuffer.currSegStart = m_uploadBuffer.offset;
}

void Renderer::startFrame() {
    // Transition the back buffer state: Presenting -> Render Target.
    const auto backBuffer = m_renderTargets[m_backBufferIndex].Get();
    const auto barrier    = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer,
                                                      D3D12_RESOURCE_STATE_PRESENT,
                                                      D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto graphicsCommandList = m_graphicsContext.commandList(0);
    graphicsCommandList->ResourceBarrier(1, &barrier);
    // Set the necessary state.
    graphicsCommandList->SetGraphicsRootSignature(m_graphicsRootSignature.Get());
    graphicsCommandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer.view);
    graphicsCommandList->RSSetViewports(1, &m_viewport);
    graphicsCommandList->RSSetScissorRects(1, &m_scissorRect);
    // Set the back buffer as the render target.
    const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvPool.getCpuHandle(m_backBufferIndex);
    const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_dsvPool.getCpuHandle(0);
    graphicsCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    // Clear the RTV and the DSV.
    constexpr float clearColor[] = {0.f, 0.f, 0.f, 1.f};
    graphicsCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    const D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
    graphicsCommandList->ClearDepthStencilView(dsvHandle, clearFlags, 0.f, 0, 0, nullptr);
    // Set the primitive/topology type.
    graphicsCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Renderer::finalizeFrame() {
    // Transition the back buffer state: Render Target -> Presenting.
    const auto backBuffer = m_renderTargets[m_backBufferIndex].Get();
    const auto barrier    = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer,
                                                      D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                      D3D12_RESOURCE_STATE_PRESENT);
    m_graphicsContext.commandList(0)->ResourceBarrier(1, &barrier);
    // Finalize and execute the command list.
    m_graphicsContext.executeCommandList(0);
    // Present the frame, and update the index of the frame buffer used for rendering.
    CHECK_CALL(m_swapChain->Present(VSYNC_INTERVAL, 0), "Failed to display the frame buffer.");
    m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();
    // Reset the command list allocator.
    m_graphicsContext.resetCommandAllocator();
    // Reset the command list to its initial state.
    m_graphicsContext.resetCommandList(0, m_graphicsPipelineState.Get());
    // Block the thread until the swap chain is ready accept a new frame.
    // Otherwise, Present() may block the thread, increasing the input lag.
    WaitForSingleObject(m_swapChainWaitableObject, INFINITE);
}

void Renderer::stop() {
    m_graphicsContext.destroy();
    m_copyContext.destroy();
}
