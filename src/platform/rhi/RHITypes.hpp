#pragma once

#include <cstdint>
#include "core/Handle.hpp"

namespace StellarAlia::RHI {

// ─────────────────────────────────────────────────────────────────────────────
// Opaque typed handles — upper layers never see the underlying GPU object
// ─────────────────────────────────────────────────────────────────────────────
using RHITextureHandle    = Core::Handle<struct RHITextureTag>;
using RHIBufferHandle     = Core::Handle<struct RHIBufferTag>;
using RHIShaderHandle     = Core::Handle<struct RHIShaderTag>;
using RHIPipelineHandle   = Core::Handle<struct RHIPipelineTag>;
using RHIDescSetHandle    = Core::Handle<struct RHIDescSetTag>;
using RHIDescLayoutHandle = Core::Handle<struct RHIDescLayoutTag>;

// ─────────────────────────────────────────────────────────────────────────────
// Texture Format
// ─────────────────────────────────────────────────────────────────────────────
enum class RHIFormat : uint32_t {
    Undefined = 0,

    // Color
    RGBA8_UNORM,
    RGBA8_SRGB,
    RGBA16F,
    RGBA32F,
    RG16F,
    RG32F,
    R8_UNORM,
    R32F,
    R32_UINT,      // Editor ID picking (Issue #102); clear only to 0 (float/uint bit-identical)
    BGRA8_UNORM,   // Common swapchain format (no gamma)
    BGRA8_SRGB,    // Common swapchain format (sRGB, most GPUs prefer this)

    // Depth / Stencil
    D32F,
    D24_S8,
    D16_UNORM,

    // Block compressed
    BC1_UNORM,
    BC3_UNORM,
    BC5_UNORM,
    BC7_UNORM,
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource State — hardware-agnostic pipeline state tracker
// Maps to VkImageLayout / D3D12_RESOURCE_STATES at the RHI backend
// ─────────────────────────────────────────────────────────────────────────────
enum class RHIResourceState : uint32_t {
    Undefined = 0,  // Initial state; contents discarded on first transition
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderRead,         // SRV / Sampled image
    UnorderedAccess,    // UAV / Storage image
    CopySrc,
    CopyDst,
    Present,            // Ready for swapchain presentation
};

// Buffer-specific pipeline states (no layout concept, only access masks).
enum class RHIBufferState : uint32_t {
    Undefined    = 0,   // Before first use in RG
    StorageRead  = 1,   // SSBO read-only  (compute / fragment)
    StorageWrite = 2,   // SSBO read+write (compute)
    IndirectRead = 3,   // Indirect draw / dispatch argument
    CopySrc      = 4,   // Transfer source
    CopyDst      = 5,   // Transfer destination (also used internally for clearOnCreate)
    VertexRead   = 6,   // Vertex buffer input
};

// ─────────────────────────────────────────────────────────────────────────────
// Usage Flags
// ─────────────────────────────────────────────────────────────────────────────
enum class RHITextureUsage : uint32_t {
    None            = 0,
    Sampled         = 1 << 0,  // Can be read in shaders
    RenderTarget    = 1 << 1,  // Can be used as color attachment
    DepthStencil    = 1 << 2,  // Can be used as depth/stencil attachment
    UnorderedAccess = 1 << 3,  // Can be written in shaders (storage)
    CopySrc         = 1 << 4,
    CopyDst         = 1 << 5,
};

inline RHITextureUsage operator|(RHITextureUsage a, RHITextureUsage b) noexcept {
    return static_cast<RHITextureUsage>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool HasFlag(RHITextureUsage flags, RHITextureUsage flag) noexcept {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

enum class RHIBufferUsage : uint32_t {
    None         = 0,
    Vertex       = 1 << 0,
    Index        = 1 << 1,
    Uniform      = 1 << 2,  // UBO / CBV
    Storage      = 1 << 3,  // SSBO / UAV buffer
    IndirectArgs = 1 << 4,  // Draw / dispatch indirect
    CopySrc      = 1 << 5,
    CopyDst      = 1 << 6,
};

inline RHIBufferUsage operator|(RHIBufferUsage a, RHIBufferUsage b) noexcept {
    return static_cast<RHIBufferUsage>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Stage Flags
// ─────────────────────────────────────────────────────────────────────────────
enum class RHIShaderStage : uint32_t {
    None     = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    All      = Vertex | Fragment | Compute,
};

inline RHIShaderStage operator|(RHIShaderStage a, RHIShaderStage b) noexcept {
    return static_cast<RHIShaderStage>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline RHIShaderStage& operator|=(RHIShaderStage& a, RHIShaderStage b) noexcept {
    a = a | b; return a;
}
inline bool HasStage(RHIShaderStage flags, RHIShaderStage stage) noexcept {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(stage)) != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Type
// ─────────────────────────────────────────────────────────────────────────────
enum class RHIDescriptorType : uint32_t {
    Texture2D,
    TextureCube,
    Sampler,
    UniformBuffer,
    StorageBuffer,
    StorageBufferDynamic,
    StorageImage,
};

// ─────────────────────────────────────────────────────────────────────────────
// Resource Descriptors
// ─────────────────────────────────────────────────────────────────────────────
struct RHITextureDesc {
    uint32_t        width        = 1;
    uint32_t        height       = 1;
    uint32_t        depth        = 1;
    uint32_t        mipLevels    = 1;
    uint32_t        arrayLayers  = 1;
    RHIFormat       format       = RHIFormat::RGBA8_UNORM;
    RHITextureUsage usage        = RHITextureUsage::Sampled;
    // When true, creates the image with VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT and
    // a VK_IMAGE_VIEW_TYPE_CUBE view. arrayLayers is forced to 6.
    bool            cubemap      = false;
    const char*     debugName    = nullptr;
};

struct RHIBufferDesc {
    uint64_t       size       = 0;
    RHIBufferUsage usage      = RHIBufferUsage::None;
    bool           cpuVisible = false;  // Maps to host-visible memory
    const char*    debugName  = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// Render Pass (dynamic rendering — no VkRenderPass object)
// ─────────────────────────────────────────────────────────────────────────────
struct RHIColorAttachment {
    RHITextureHandle texture;
    uint32_t         mipLevel     = 0;
    uint32_t         layer        = 0;
    bool             clearOnLoad  = false;
    float            clearColor[4] = {0.f, 0.f, 0.f, 1.f};
};

struct RHIDepthAttachment {
    RHITextureHandle texture;
    bool             clearOnLoad  = true;
    float            clearDepth   = 1.f;
    uint8_t          clearStencil = 0;
    // Issue #56: bind in DEPTH_STENCIL_READ_ONLY layout — for passes that
    // stencil-test while simultaneously sampling the depth plane (deferred
    // lighting). Texture must be in DepthRead state (RGPassBuilder::
    // ReadDepthStencil) and the pipeline must not write depth/stencil.
    bool             readOnly     = false;
};

struct RHIRenderPassDesc {
    RHIColorAttachment colorAttachments[8] = {};
    uint32_t           colorAttachmentCount = 0;
    RHIDepthAttachment depthAttachment      = {};
    bool               hasDepth             = false;
    uint32_t           width                = 0;
    uint32_t           height               = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Viewport & Scissor
// ─────────────────────────────────────────────────────────────────────────────
struct RHIViewport {
    float    x = 0.f, y = 0.f;
    float    width, height;
    float    minDepth = 0.f, maxDepth = 1.f;
};

struct RHIScissor {
    int32_t  offsetX = 0, offsetY = 0;
    uint32_t width, height;
};

// ─────────────────────────────────────────────────────────────────────────────
// NativeWindowHandle — opaque wrapper around a platform window pointer.
// The RHI backend casts ptr to the appropriate type (GLFWwindow*, HWND...).
// Upper layers only pass it through; they never cast or dereference it.
// ─────────────────────────────────────────────────────────────────────────────
struct NativeWindowHandle {
    void* ptr = nullptr;
    [[nodiscard]] bool IsValid() const noexcept { return ptr != nullptr; }
};

// ─────────────────────────────────────────────────────────────────────────────
// RHIDeviceDesc — passed to IRHIDevice::Create() / factory functions.
// ─────────────────────────────────────────────────────────────────────────────
struct RHIDeviceDesc {
    NativeWindowHandle windowHandle;
    uint32_t           swapchainWidth      = 1280;
    uint32_t           swapchainHeight     = 720;
    uint32_t           swapchainImageCount = 2;    // Double-buffering default
    bool               vsync               = true;
    bool               enableValidation    = false; // Validation layers (Vulkan)
};

} // namespace StellarAlia::RHI
