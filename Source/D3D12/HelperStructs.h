#pragma once

#include <d3d12.h>
#include <utility>
#include <wrl\client.h>
#include "..\Common\Constants.h"
#include "..\Common\Definitions.h"

namespace D3D12 {
    using Microsoft::WRL::ComPtr;

    // Corresponds to Direct3D descriptor types
    enum class DescType {
        // Constant Buffer Views | Shader Resource Views | Unordered Access Views
        CBV_SRV_UAV = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 
        SAMPLER     = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,     // Samplers
        RTV         = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,         // Render Target Views
        DSV         = D3D12_DESCRIPTOR_HEAP_TYPE_DSV          // Depth Stencil Views
    };

    // Corresponds to Direct3D command list types
    enum class QueueType {
        GRAPHICS = D3D12_COMMAND_LIST_TYPE_DIRECT,  // Supports all types of commands
        COMPUTE  = D3D12_COMMAND_LIST_TYPE_COMPUTE, // Supports compute and copy commands only
        COPY     = D3D12_COMMAND_LIST_TYPE_COPY     // Supports copy commands only
    };

    struct UploadRingBuffer {
        RULE_OF_FIVE_MOVE_ONLY(UploadRingBuffer);
        UploadRingBuffer();
        ComPtr<ID3D12Resource>       resource;      // Buffer interface
        byte*                        begin;         // CPU virtual memory-mapped address
        uint                         capacity;      // Buffer size in bytes
        uint                         offset;        // Offset from the beginning of the buffer
        uint                         prevSegStart;  // Offset to the beginning of the prev. segment
        uint                         currSegStart;  // Offset to the beginning of the curr. segment
    };

    struct VertexBuffer {
        ComPtr<ID3D12Resource>       resource;      // Buffer interface
        D3D12_VERTEX_BUFFER_VIEW     view;          // Buffer descriptor
    };

    struct IndexBuffer {
        ComPtr<ID3D12Resource>       resource;      // Buffer interface
        D3D12_INDEX_BUFFER_VIEW      view;          // Buffer descriptor
        uint                         count() const; // Returns the number of elements
    };

    struct ConstantBuffer {
        ComPtr<ID3D12Resource>       resource;      // Buffer interface
        D3D12_GPU_VIRTUAL_ADDRESS    location;      // GPU virtual address of the buffer
    };

    using D3D12_SHADER_RESOURCE_VIEW = D3D12_SHADER_RESOURCE_VIEW_DESC;

    struct ShaderResource {
        ComPtr<ID3D12Resource>       resource;      // Buffer interface
        D3D12_SHADER_RESOURCE_VIEW   view;          // Buffer descriptor
    };

    // Descriptor heap wrapper
    template <DescType T>
    struct DescriptorPool {
        ComPtr<ID3D12DescriptorHeap> heap;          // Descriptor heap interface
        D3D12_CPU_DESCRIPTOR_HANDLE  cpuBegin;      // CPU handle of the 1st descriptor of the pool
        D3D12_GPU_DESCRIPTOR_HANDLE  gpuBegin;      // GPU handle of the 1st descriptor of the pool
        uint                         handleIncrSz;  // Handle increment size
    };

    // Command queue extension with N allocators
    template <QueueType T, uint N>
    struct CommandQueueEx {
    public:
        RULE_OF_ZERO_MOVE_ONLY(CommandQueueEx);
        CommandQueueEx() = default;
        // Submits a single command list for execution
        void execute(ID3D12CommandList* const commandList) const;
        // Submits K command lists for execution
        template <uint K>
        void execute(ID3D12CommandList* const (&commandLists)[K]) const;
        // Inserts the fence into the queue
        // Optionally, a custom value of the fence can be specified
        // Returns the inserted fence and its value
        std::pair<ID3D12Fence*, uint64> insertFence(const uint64 customFenceValue = 0);
        // Blocks the execution of the thread until the fence is reached
        // Optionally, a custom value of the fence can be specified
        void syncThread(const uint64 customFenceValue = 0);
        // Blocks the execution of the queue until the fence
        // with the specified value is reached
        void syncQueue(ID3D12Fence* const fence, const uint64 fenceValue);
        // Waits for the queue to become drained, and stops synchronization
        void finish();
        /* Accessors */
        ID3D12CommandQueue*            get();
        const ID3D12CommandQueue*      get() const;
        ID3D12CommandAllocator*        listAlloca();
        const ID3D12CommandAllocator*  listAlloca() const;
    private:
        ComPtr<ID3D12CommandQueue>     m_interface;     // Command queue interface
        ComPtr<ID3D12CommandAllocator> m_listAlloca[N]; // Command list allocator interface
        /* Synchronization objects */
        ComPtr<ID3D12Fence>            m_fence;
        uint64                         m_fenceValue;
        HANDLE                         m_syncEvent;
        /* Accessors */
        friend struct ID3D12DeviceEx;
    };

    template <uint N> using GraphicsCommandQueueEx = CommandQueueEx<QueueType::GRAPHICS, N>;
    template <uint N> using ComputeCommandQueueEx  = CommandQueueEx<QueueType::COMPUTE, N>;
    template <uint N> using CopyCommandQueueEx     = CommandQueueEx<QueueType::COPY, N>;

    // ID3D12Device extension; uses the same UUID as ID3D12Device
    MIDL_INTERFACE("189819f1-1db6-4b57-be54-1821339b85f7")
    ID3D12DeviceEx: public ID3D12Device {
    public:
        RULE_OF_ZERO(ID3D12DeviceEx);
        template<QueueType T, uint N>
        void createCommandQueue(CommandQueueEx<T, N>* const commandQueue, 
                                const bool isHighPriority    = false, 
                                const bool disableGpuTimeout = false);
        // Creates a descriptor pool of the specified size (descriptor count) and type
        template <DescType T>
        void createDescriptorPool(DescriptorPool<T>* const descriptorPool, const uint count);
        // Creates a command queue of the specified type
        // Optionally, the queue priority can be set to 'high', and the GPU timeout can be disabled
        // Multi-GPU-adapter mask. Rendering is performed on a single GPU
        static constexpr uint nodeMask = 0;
    };
} // namespace D3D12
