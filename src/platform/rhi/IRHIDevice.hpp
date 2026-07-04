#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>

#include "platform/rhi/RHITypes.hpp"
#include "platform/rhi/ShaderReflection.hpp"

namespace StellarAlia::RHI {

class IRHICommandList;

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline creation descriptor
// Populated by the Resource Layer once shader reflection is available.
// ─────────────────────────────────────────────────────────────────────────────

enum class RHICullMode   : uint8_t { None, Back, Front };
enum class RHIBlendMode  : uint8_t { Opaque, AlphaBlend, Additive };
enum class RHITopology   : uint8_t { TriangleList, TriangleStrip, LineList };

// Issue #56 — depth compare + fixed-function stencil.
enum class RHICompareOp  : uint8_t { Never, Less, Equal, LessOrEqual, Greater,
                                     NotEqual, GreaterOrEqual, Always };
enum class RHIStencilOp  : uint8_t { Keep, Zero, Replace, IncrClamp, DecrClamp,
                                     Invert, IncrWrap, DecrWrap };

struct RHIStencilOpState {
    RHIStencilOp failOp      = RHIStencilOp::Keep;
    RHIStencilOp passOp      = RHIStencilOp::Keep;
    RHICompareOp compareOp   = RHICompareOp::Always;
    uint8_t      reference   = 0;
    uint8_t      compareMask = 0xFF;
    uint8_t      writeMask   = 0xFF;

    bool operator==(const RHIStencilOpState&) const noexcept = default;
};

struct RHIPipelineDesc {
    RHIShaderHandle     vertShader;
    RHIShaderHandle     fragShader;

    // Ordered list of descriptor set layouts.
    // Index i = set i in the pipeline layout (set=0, set=1, …).
    // Set to invalid handles for unused slots; trailing invalid handles are ignored.
    RHIDescLayoutHandle descriptorLayouts[4] = {};
    uint32_t            descriptorLayoutCount = 0;

    uint32_t            pushConstantSize   = 0;
    RHIShaderStage      pushConstantStages = RHIShaderStage::None;

    // Render target formats (dynamic rendering — must match the pass attachment formats)
    RHIFormat           colorFormats[8]  = {};
    uint32_t            colorFormatCount = 0;
    RHIFormat           depthFormat      = RHIFormat::Undefined;

    // Render state
    RHICullMode         cullMode         = RHICullMode::Back;
    RHIBlendMode        blendMode        = RHIBlendMode::Opaque;
    RHITopology         topology         = RHITopology::TriangleList;
    bool                depthTest        = true;
    bool                depthWrite       = true;
    // Issue #56: was hardcoded LESS_OR_EQUAL in the backend. EQUAL is used by
    // the GBuffer main pass for prepass-filled (MASK) geometry.
    RHICompareOp        depthCompareOp   = RHICompareOp::LessOrEqual;

    // Issue #56: fixed-function stencil. The stencil attachment format is derived
    // from depthFormat (D24_S8 → same) so every pipeline sharing a depth+stencil
    // attachment stays render-pass compatible without per-caller bookkeeping.
    // Defaults keep every existing pipeline bit-identical.
    bool                stencilTestEnable  = false;
    bool                stencilWriteEnable = false;
    RHIStencilOpState   stencilFront;
    RHIStencilOpState   stencilBack;

    // When true, vertex input is skipped (gl_VertexIndex fullscreen-tri trick)
    bool                noVertexInput    = false;

    // Reflection-driven vertex input. Populated from ShaderReflection::vertexInputs
    // by ShaderProgram; the backend emits one VkVertexInputAttributeDescription per
    // entry. Empty (count == 0) with noVertexInput == false → backend falls back to
    // the legacy 4-attrib hardcoded layout for backward compat with v3-v5 .refl.
    static constexpr uint32_t kMaxVertexAttribs = 8;
    ShaderVertexInputDesc vertexInputs[kMaxVertexAttribs] = {};
    uint32_t              vertexInputCount = 0;

    const char*         debugName        = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Compute pipeline creation descriptor
// ─────────────────────────────────────────────────────────────────────────────
struct RHIComputePipelineDesc {
    RHIShaderHandle     computeShader;

    RHIDescLayoutHandle descriptorLayouts[4] = {};
    uint32_t            descriptorLayoutCount = 0;

    uint32_t            pushConstantSize   = 0;

    const char*         debugName          = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// GPU memory statistics — filled by IRHIDevice::GetMemoryStats().
// gpuUsedBytes / gpuBudgetBytes come from the allocator's heap-level view
// (includes alignment overhead; 0 budget = extension unavailable).
// gpuTextureBytes / gpuBufferBytes are logical sums from the device's tables.
// ─────────────────────────────────────────────────────────────────────────────
struct RHIMemoryStats {
    uint64_t gpuTextureBytes = 0; // sum of all live non-swapchain textures (logical)
    uint64_t gpuBufferBytes  = 0; // sum of all live buffers (logical)
    uint64_t gpuUsedBytes    = 0; // actual VRAM usage per allocator (incl. alignment)
    uint64_t gpuBudgetBytes  = 0; // estimated VRAM budget (0 if unavailable)
};

// ─────────────────────────────────────────────────────────────────────────────
// IRHIDevice
//
// Central resource factory and frame driver.
// The concrete implementation (VulkanDevice, D3D12Device...) is the only place
// in the engine that sees VkImage / ID3D12Resource / VmaAllocation etc.
//
// Ownership convention:
//   All handles are owned by the device.
//   Destroy*() must be called before the device is destroyed.
//   The device does NOT reference-count handles automatically.
// ─────────────────────────────────────────────────────────────────────────────
class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // ── Resource Creation ─────────────────────────────────────────────────────

    [[nodiscard]] virtual RHITextureHandle CreateTexture(
        const RHITextureDesc& desc) = 0;

    [[nodiscard]] virtual RHIBufferHandle CreateBuffer(
        const RHIBufferDesc& desc) = 0;

    // spirv:      raw SPIR-V bytecode loaded from a .spv file
    // reflection: pre-parsed binding metadata loaded from a companion .refl file
    [[nodiscard]] virtual RHIShaderHandle CreateShader(
        std::span<const uint8_t> spirv,
        const ShaderReflection&  reflection) = 0;

    // ── Reflection-Driven Layout & Pipeline ───────────────────────────────────

    // Build a descriptor set layout for a single set index from merged reflection.
    // Cached internally by the implementation — calling twice with equal inputs
    // returns the same handle.
    [[nodiscard]] virtual RHIDescLayoutHandle CreateDescriptorSetLayout(
        const ShaderReflection& merged,
        uint32_t                set) = 0;

    // Build a single-binding descriptor set layout containing a fixed-size
    // sampler2D array (UPDATE_AFTER_BIND + PARTIALLY_BOUND). Used by the
    // bindless texture heap.
    [[nodiscard]] virtual RHIDescLayoutHandle CreateBindlessTextureLayout(
        uint32_t capacity) = 0;

    [[nodiscard]] virtual RHIPipelineHandle CreatePipeline(
        const RHIPipelineDesc& desc) = 0;

    // ── Compute Pipeline ──────────────────────────────────────────────────────
    [[nodiscard]] virtual RHIPipelineHandle CreateComputePipeline(
        const RHIComputePipelineDesc& desc) = 0;

    // ── Descriptor Set ────────────────────────────────────────────────────────

    [[nodiscard]] virtual RHIDescSetHandle AllocateDescriptorSet(
        RHIDescLayoutHandle layout) = 0;

    virtual void FreeDescriptorSet(RHIDescSetHandle ds) = 0;

    // depthStencilReadLayout (Issue #56): write the descriptor with
    // DEPTH_STENCIL_READ_ONLY layout — for sampling the depth plane of an
    // image that is simultaneously bound as a read-only depth/stencil
    // attachment (deferred lighting stencil masking).
    virtual void WriteDescriptorTexture(RHIDescSetHandle ds,
                                        uint32_t         binding,
                                        RHITextureHandle texture,
                                        bool             depthStencilReadLayout = false) = 0;

    // Writes texture to a specific array element of a descriptor binding.
    // Used by the bindless texture heap (set=3 binding=0, fixed-size array).
    virtual void WriteDescriptorTextureArray(RHIDescSetHandle ds,
                                             uint32_t         binding,
                                             uint32_t         arrayElement,
                                             RHITextureHandle texture) = 0;

    // Writes a texture as a storage image (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE /
    // VK_IMAGE_LAYOUT_GENERAL). Used for compute UAV bindings.
    virtual void WriteDescriptorStorageImage(RHIDescSetHandle ds,
                                             uint32_t         binding,
                                             RHITextureHandle texture) = 0;

    // Writes a single mip level of a texture as a storage image UAV.
    // Used for per-mip compute writes (e.g. prefiltered env mip chain).
    virtual void WriteDescriptorStorageImageMip(RHIDescSetHandle ds,
                                                uint32_t         binding,
                                                RHITextureHandle texture,
                                                uint32_t         mipLevel) = 0;

    // Writes a single mip level into a specific array element of a storage-image
    // array binding (VK dstArrayElement). For SPD-style mip-chain generation
    // where array element i binds mip level i (Issue #94). Reuses the per-mip view.
    virtual void WriteDescriptorStorageImageArrayMip(RHIDescSetHandle ds,
                                                     uint32_t         binding,
                                                     uint32_t         arrayElement,
                                                     RHITextureHandle texture,
                                                     uint32_t         mipLevel) = 0;

    virtual void WriteDescriptorBuffer(RHIDescSetHandle ds,
                                       uint32_t         binding,
                                       RHIBufferHandle  buffer,
                                       uint64_t         offset  = 0,
                                       uint64_t         range   = ~0ull,
                                       // Issue #72: set true when the destination binding is
                                       // STORAGE_BUFFER_DYNAMIC / UNIFORM_BUFFER_DYNAMIC, so
                                       // vkUpdateDescriptorSets emits the matching descriptor type.
                                       bool             dynamic = false) = 0;

    // ── Data Upload ───────────────────────────────────────────────────────────

    // Convenience upload for CPU-visible buffers.
    // For GPU-only buffers use CopyBuffer via a staging buffer.
    virtual void UploadBufferData(RHIBufferHandle buffer,
                                  const void*     data,
                                  uint64_t        size,
                                  uint64_t        offset = 0) = 0;

    // Read back data from a CPU-visible buffer (persistent VMA mapping).
    // Must only be called for buffers created with cpuVisible=true.
    // Safe to call after the GPU fence has been waited (e.g. after BeginFrame).
    virtual void ReadBufferData(RHIBufferHandle buffer,
                                void*           data,
                                uint64_t        size,
                                uint64_t        offset = 0) = 0;

    // Upload initial pixel data to a GPU-only texture via an internal staging buffer.
    // Allocs staging → memcpy → one-shot submit → transitions layout to ShaderRead → frees staging.
    // Only uploads mip level 0. For multi-mip textures use UploadTextureMips.
    virtual void UploadTextureData(RHITextureHandle handle,
                                   const void*      data,
                                   uint64_t         size) = 0;

    // Per-mip data span passed to UploadTextureMips.
    struct MipUpload { const void* data; uint64_t size; };

    // Upload all mip levels in a single command submit.
    // mips[i] corresponds to mip level i; mips.size() must equal the texture's mipLevels.
    virtual void UploadTextureMips(RHITextureHandle           handle,
                                   std::span<const MipUpload> mips) = 0;

    // Per-mip destination buffer passed to ReadbackTextureMips.
    struct MipReadback { void* data; uint64_t size; };

    // GPU → CPU readback.  Copies all mip levels of a texture to caller-supplied buffers.
    // mips[i].data must point to a buffer of at least mips[i].size bytes.
    // mips.size() must equal the texture's mipLevels.
    // The texture must have been created with RHITextureUsage::CopySrc.
    // Blocks until the GPU readback completes.
    virtual void ReadbackTextureMips(RHITextureHandle       handle,
                                     std::span<MipReadback> mips) = 0;

    // Returns the descriptor used to create this texture (width, height, format, etc.).
    // Returns nullptr for invalid handles.
    [[nodiscard]] virtual const RHITextureDesc* GetTextureDesc(
        RHITextureHandle handle) const = 0;

    // Returns a snapshot of GPU memory usage.
    // Cheap to call (heap-level query + table iteration); safe every frame.
    [[nodiscard]] virtual RHIMemoryStats GetMemoryStats() const = 0;

    // Returns the selected GPU's device name (e.g. "NVIDIA GeForce RTX 3070").
    // Valid for the lifetime of the device object.
    [[nodiscard]] virtual std::string_view GetDeviceName() const = 0;

    // ── Resource Destruction ──────────────────────────────────────────────────
    // Safe to call with an invalid handle (no-op).

    virtual void DestroyTexture(RHITextureHandle  handle) = 0;
    virtual void DestroyBuffer(RHIBufferHandle   handle) = 0;
    virtual void DestroyShader(RHIShaderHandle   handle) = 0;
    virtual void DestroyPipeline(RHIPipelineHandle handle) = 0;

    // ── Frame Control ─────────────────────────────────────────────────────────
    // BeginFrame: acquires swapchain image, resets command buffer, begins recording.
    // EndFrame:   ends recording.
    // Present:    submits + presents.

    [[nodiscard]] virtual IRHICommandList* BeginFrame() = 0;
    virtual void EndFrame()  = 0;
    virtual void Present()   = 0;

    // Flush all GPU work. Use for shutdown or resource streaming.
    virtual void WaitIdle()  = 0;

    // Submit one-shot compute work outside the frame loop.
    // Records commands into an immediate command buffer, submits to the
    // GPU queue, and waits for completion before returning.
    // Use for startup initialization (IBL bake, asset streaming, etc.)
    // that must complete before the first frame begins.
    virtual void ImmediateCompute(std::function<void(IRHICommandList*)> fn) = 0;

    // ── Swapchain ─────────────────────────────────────────────────────────────
    // GetSwapchainTexture(): valid only between BeginFrame() and EndFrame().
    // Returns the back-buffer texture for the current in-flight frame.
    // RenderGraph imports this as an external texture for the final blit / UI pass.

    [[nodiscard]] virtual RHITextureHandle GetSwapchainTexture() = 0;
    [[nodiscard]] virtual RHIFormat        GetSwapchainFormat()  = 0;
    [[nodiscard]] virtual uint32_t         GetSwapchainWidth()   = 0;
    [[nodiscard]] virtual uint32_t         GetSwapchainHeight()  = 0;

    // Called when the OS window is resized (e.g. from GLFWWindow resize callback).
    // The implementation recreates the swapchain and any size-dependent resources.
    virtual void ResizeSwapchain(uint32_t width, uint32_t height) = 0;

    // Returns the current in-flight frame slot index [0..MAX_FRAMES-1].
    // Valid after BeginFrame() returns a non-null command list.
    [[nodiscard]] virtual uint32_t GetCurrentFrameIndex() const = 0;

    // ── Device Limits ─────────────────────────────────────────────────────────

    // Minimum alignment for STORAGE_BUFFER_DYNAMIC offsets.
    // Used by per-frame SSBO ring allocators (MaterialParamRing etc.).
    [[nodiscard]] virtual uint32_t GetMinStorageBufferOffsetAlignment() const = 0;
};

} // namespace StellarAlia::RHI
