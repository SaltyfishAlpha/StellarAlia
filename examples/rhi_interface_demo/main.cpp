/**
 * rhi_interface_demo — Stage 1 validation
 *
 * Implements NullRHIDevice and NullRHICommandList: concrete classes that satisfy
 * the IRHIDevice / IRHICommandList contracts without touching any real GPU.
 * Every call is logged so the demo output can be read as a trace of what a real
 * backend would execute.
 *
 * Scenario (mirrors a single deferred geometry pass):
 *   1. Create textures (albedo, normal, G-buffer color, depth)
 *   2. Create a uniform buffer (camera UBO)
 *   3. Hand-craft shader reflections that spirv-reflect would produce at build time
 *   4. Merge vert + frag reflections → auto-derive descriptor set layout
 *   5. Build a pipeline from the merged layout
 *   6. Write descriptors using variable names from reflection (name → binding lookup)
 *   7. Simulate one frame: BeginFrame → barriers → render pass → draw → EndFrame
 *   8. Destroy all resources
 */

#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/logs/Log.hpp"
#include "platform/rhi/IRHICommandList.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/ShaderReflection.hpp"

using namespace StellarAlia;
using namespace StellarAlia::RHI;

// ─────────────────────────────────────────────────────────────────────────────
// NullRHICommandList
// ─────────────────────────────────────────────────────────────────────────────
class NullRHICommandList final : public IRHICommandList {
public:
    void BeginRenderPass(const RHIRenderPassDesc& desc) override {
        SA_LOG_INFO("[CMD] BeginRenderPass  attachments={} hasDepth={}",
                    desc.colorAttachmentCount, desc.hasDepth);
    }
    void EndRenderPass() override {
        SA_LOG_INFO("[CMD] EndRenderPass");
    }
    void SetViewport(const RHIViewport& vp) override {
        SA_LOG_INFO("[CMD] SetViewport  {}x{} @ ({},{})", vp.width, vp.height, vp.x, vp.y);
    }
    void SetScissor(const RHIScissor& sc) override {
        SA_LOG_INFO("[CMD] SetScissor   {}x{}", sc.width, sc.height);
    }
    void SetPipeline(RHIPipelineHandle pipeline) override {
        SA_LOG_INFO("[CMD] SetPipeline  handle={}", pipeline.index);
    }
    void SetDescriptorSet(uint32_t set, RHIDescSetHandle ds,
                          std::span<const uint32_t> dynamicOffsets = {}) override {
        SA_LOG_INFO("[CMD] SetDescriptorSet  set={} handle={} dynOffs={}",
                    set, ds.index, dynamicOffsets.size());
    }
    void SetPushConstants(const void*, uint32_t size, RHIShaderStage stages) override {
        SA_LOG_INFO("[CMD] SetPushConstants  size={}B stages=0x{:x}",
                    size, static_cast<uint32_t>(stages));
    }
    void SetVertexBuffer(uint32_t slot, RHIBufferHandle buf, uint64_t offset) override {
        SA_LOG_INFO("[CMD] SetVertexBuffer  slot={} handle={} offset={}", slot, buf.index, offset);
    }
    void SetIndexBuffer(RHIBufferHandle buf, uint64_t offset, bool use16bit) override {
        SA_LOG_INFO("[CMD] SetIndexBuffer   handle={} offset={} 16bit={}", buf.index, offset, use16bit);
    }
    void Draw(uint32_t vtx, uint32_t inst, uint32_t, uint32_t) override {
        SA_LOG_INFO("[CMD] Draw  vertices={} instances={}", vtx, inst);
    }
    void DrawIndexed(uint32_t idx, uint32_t inst, uint32_t, int32_t, uint32_t) override {
        SA_LOG_INFO("[CMD] DrawIndexed  indices={} instances={}", idx, inst);
    }
    void SetComputePipeline(RHIPipelineHandle pipeline) override {
        SA_LOG_INFO("[CMD] SetComputePipeline  handle={}", pipeline.index);
    }
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override {
        SA_LOG_INFO("[CMD] Dispatch  ({},{},{})", x, y, z);
    }
    void TransitionTexture(RHITextureHandle tex,
                           RHIResourceState from,
                           RHIResourceState to) override {
        SA_LOG_INFO("[CMD] Barrier  texture={} {} → {}",
                    tex.index,
                    static_cast<uint32_t>(from),
                    static_cast<uint32_t>(to));
    }
    void CopyBuffer(RHIBufferHandle src, RHIBufferHandle dst,
                    uint64_t srcOff, uint64_t dstOff, uint64_t size) override {
        SA_LOG_INFO("[CMD] CopyBuffer  src={} dst={} size={}", src.index, dst.index, size);
    }
    void CopyBufferToTexture(RHIBufferHandle src, RHITextureHandle dst,
                             uint32_t mip, uint32_t layer) override {
        SA_LOG_INFO("[CMD] CopyBufferToTexture  buf={} tex={} mip={} layer={}",
                    src.index, dst.index, mip, layer);
    }
    void FillBuffer(RHIBufferHandle buffer, uint64_t offset, uint64_t size,
                    uint32_t value) override {
        SA_LOG_INFO("[CMD] FillBuffer  buf={} offset={} size={} value={}",
                    buffer.index, offset, size, value);
    }
    void BufferBarrier(RHIBufferHandle buffer, RHIBufferState from,
                       RHIBufferState to) override {
        SA_LOG_INFO("[CMD] BufferBarrier  buf={} {}->{}", buffer.index,
                    static_cast<uint32_t>(from), static_cast<uint32_t>(to));
    }
    void GenerateMipmaps(RHITextureHandle texture) override {
        SA_LOG_INFO("[CMD] GenerateMipmaps  tex={}", texture.index);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// NullRHIDevice — slot-map backed resource registry + command list owner
// ─────────────────────────────────────────────────────────────────────────────
class NullRHIDevice final : public IRHIDevice {
    // Simple monotonic handle allocator (no reuse for this demo)
    uint32_t m_nextTexture    = 0;
    uint32_t m_nextBuffer     = 0;
    uint32_t m_nextShader     = 0;
    uint32_t m_nextPipeline   = 0;
    uint32_t m_nextDescSet    = 0;
    uint32_t m_nextDescLayout = 0;

    // Track alive resources (name lookup for debug output)
    struct TextureEntry { RHITextureDesc desc; };
    struct BufferEntry  { RHIBufferDesc  desc; };
    std::unordered_map<uint32_t, TextureEntry> m_textures;
    std::unordered_map<uint32_t, BufferEntry>  m_buffers;

    NullRHICommandList m_cmd;

public:
    // ── Resource Creation ─────────────────────────────────────────────────────

    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override {
        RHITextureHandle h{m_nextTexture++};
        m_textures[h.index] = {desc};
        SA_LOG_INFO("[DEV] CreateTexture  handle={} '{}' {}x{}",
                    h.index,
                    desc.debugName ? desc.debugName : "(unnamed)",
                    desc.width, desc.height);
        return h;
    }

    RHIBufferHandle CreateBuffer(const RHIBufferDesc& desc) override {
        RHIBufferHandle h{m_nextBuffer++};
        m_buffers[h.index] = {desc};
        SA_LOG_INFO("[DEV] CreateBuffer  handle={} '{}' size={}B cpuVisible={}",
                    h.index,
                    desc.debugName ? desc.debugName : "(unnamed)",
                    desc.size, desc.cpuVisible);
        return h;
    }

    RHIShaderHandle CreateShader(std::span<const uint8_t> spirv,
                                 const ShaderReflection&  refl) override {
        RHIShaderHandle h{m_nextShader++};
        SA_LOG_INFO("[DEV] CreateShader  handle={} spirvBytes={} bindings={}",
                    h.index, spirv.size(), refl.bindings.size());
        for (const auto& b : refl.bindings)
            SA_LOG_INFO("        binding set={} idx={} name='{}' stages=0x{:x}",
                        b.set, b.binding, b.name, static_cast<uint32_t>(b.stages));
        return h;
    }

    // ── Reflection-Driven Layout & Pipeline ───────────────────────────────────

    RHIDescLayoutHandle CreateDescriptorSetLayout(const ShaderReflection& merged,
                                                  uint32_t set) override {
        RHIDescLayoutHandle h{m_nextDescLayout++};
        SA_LOG_INFO("[DEV] CreateDescriptorSetLayout  handle={} set={}", h.index, set);
        for (const auto& b : merged.bindings)
            if (b.set == set)
                SA_LOG_INFO("        binding idx={} name='{}' type={}",
                            b.binding, b.name, static_cast<uint32_t>(b.type));
        return h;
    }

    RHIDescLayoutHandle CreateBindlessTextureLayout(uint32_t capacity) override {
        RHIDescLayoutHandle h{m_nextDescLayout++};
        SA_LOG_INFO("[DEV] CreateBindlessTextureLayout  handle={} capacity={}",
                    h.index, capacity);
        return h;
    }

    RHIPipelineHandle CreatePipeline(const RHIPipelineDesc& desc) override {
        RHIPipelineHandle h{m_nextPipeline++};
        SA_LOG_INFO("[DEV] CreatePipeline  handle={} vert={} frag={} layouts={} pushBytes={}",
                    h.index,
                    desc.vertShader.index,
                    desc.fragShader.index,
                    desc.descriptorLayoutCount,
                    desc.pushConstantSize);
        return h;
    }

    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc& desc) override {
        RHIPipelineHandle h{m_nextPipeline++};
        SA_LOG_INFO("[DEV] CreateComputePipeline  handle={} shader={} layouts={} pushBytes={}",
                    h.index,
                    desc.computeShader.index,
                    desc.descriptorLayoutCount,
                    desc.pushConstantSize);
        return h;
    }

    // ── Descriptor Set ────────────────────────────────────────────────────────

    RHIDescSetHandle AllocateDescriptorSet(RHIDescLayoutHandle layout) override {
        RHIDescSetHandle h{m_nextDescSet++};
        SA_LOG_INFO("[DEV] AllocateDescriptorSet  handle={} layout={}", h.index, layout.index);
        return h;
    }

    void FreeDescriptorSet(RHIDescSetHandle ds) override {
        SA_LOG_INFO("[DEV] FreeDescriptorSet  handle={}", ds.index);
    }

    void WriteDescriptorTexture(RHIDescSetHandle ds,
                                uint32_t         binding,
                                RHITextureHandle texture) override {
        SA_LOG_INFO("[DEV] WriteDescriptor  ds={} binding={} <- texture={}",
                    ds.index, binding, texture.index);
    }

    void WriteDescriptorTextureArray(RHIDescSetHandle ds,
                                     uint32_t         binding,
                                     uint32_t         arrayElement,
                                     RHITextureHandle texture) override {
        SA_LOG_INFO("[DEV] WriteDescriptorArray  ds={} binding={}[{}] <- texture={}",
                    ds.index, binding, arrayElement, texture.index);
    }

    void WriteDescriptorStorageImage(RHIDescSetHandle ds,
                                     uint32_t         binding,
                                     RHITextureHandle texture) override {
        SA_LOG_INFO("[DEV] WriteDescriptor  ds={} binding={} <- storageImage={}",
                    ds.index, binding, texture.index);
    }

    void WriteDescriptorStorageImageMip(RHIDescSetHandle ds,
                                        uint32_t         binding,
                                        RHITextureHandle texture,
                                        uint32_t         mipLevel) override {
        SA_LOG_INFO("[DEV] WriteDescriptor  ds={} binding={} <- storageImage={} mip={}",
                    ds.index, binding, texture.index, mipLevel);
    }

    void WriteDescriptorStorageImageArrayMip(RHIDescSetHandle ds,
                                             uint32_t         binding,
                                             uint32_t         arrayElement,
                                             RHITextureHandle texture,
                                             uint32_t         mipLevel) override {
        ++m_storageArrayWriteCount;
        SA_LOG_INFO("[DEV] WriteDescriptor  ds={} binding={}[{}] <- storageImage={} mip={}",
                    ds.index, binding, arrayElement, texture.index, mipLevel);
    }

    // Counter exercised by the Issue #94 storage-image-array unit test in main().
    uint32_t m_storageArrayWriteCount = 0;

    void WriteDescriptorBuffer(RHIDescSetHandle ds,
                               uint32_t         binding,
                               RHIBufferHandle  buffer,
                               uint64_t         /*offset*/ = 0,
                               uint64_t         /*range*/  = ~0ull,
                               bool             /*dynamic*/ = false) override {
        SA_LOG_INFO("[DEV] WriteDescriptor  ds={} binding={} <- buffer={}",
                    ds.index, binding, buffer.index);
    }

    // ── Data Upload ───────────────────────────────────────────────────────────

    void UploadBufferData(RHIBufferHandle buf, const void*, uint64_t size, uint64_t offset) override {
        SA_LOG_INFO("[DEV] UploadBufferData  buffer={} size={}B offset={}", buf.index, size, offset);
    }

    void ReadBufferData(RHIBufferHandle buf, void*, uint64_t size, uint64_t offset) override {
        SA_LOG_INFO("[DEV] ReadBufferData  buffer={} size={}B offset={}", buf.index, size, offset);
    }

    void UploadTextureData(RHITextureHandle tex, const void*, uint64_t size) override {
        SA_LOG_INFO("[DEV] UploadTextureData  texture={} size={}B", tex.index, size);
    }

    void UploadTextureMips(RHITextureHandle tex,
                           std::span<const MipUpload> mips) override {
        SA_LOG_INFO("[DEV] UploadTextureMips  texture={} mips={}", tex.index, mips.size());
    }

    void ReadbackTextureMips(RHITextureHandle tex,
                             std::span<MipReadback> mips) override {
        SA_LOG_INFO("[DEV] ReadbackTextureMips  texture={} mips={}", tex.index, mips.size());
    }

    void ImmediateCompute(std::function<void(IRHICommandList*)> fn) override {
        SA_LOG_INFO("[DEV] ImmediateCompute  begin");
        fn(&m_cmd);
        SA_LOG_INFO("[DEV] ImmediateCompute  end");
    }

    uint32_t GetCurrentFrameIndex() const override { return 0; }
    uint32_t GetMinStorageBufferOffsetAlignment() const override { return 16; }

    // ── Destruction ───────────────────────────────────────────────────────────

    void DestroyTexture(RHITextureHandle h) override {
        if (!h.IsValid()) return;
        m_textures.erase(h.index);
        SA_LOG_INFO("[DEV] DestroyTexture  handle={}", h.index);
    }
    void DestroyBuffer(RHIBufferHandle h) override {
        if (!h.IsValid()) return;
        m_buffers.erase(h.index);
        SA_LOG_INFO("[DEV] DestroyBuffer  handle={}", h.index);
    }
    void DestroyShader(RHIShaderHandle h) override {
        if (!h.IsValid()) return;
        SA_LOG_INFO("[DEV] DestroyShader  handle={}", h.index);
    }
    void DestroyPipeline(RHIPipelineHandle h) override {
        if (!h.IsValid()) return;
        SA_LOG_INFO("[DEV] DestroyPipeline  handle={}", h.index);
    }

    // ── Frame Control ─────────────────────────────────────────────────────────

    IRHICommandList* BeginFrame() override {
        SA_LOG_INFO("[DEV] ---- BeginFrame ----");
        return &m_cmd;
    }
    void EndFrame() override {
        SA_LOG_INFO("[DEV] ---- EndFrame ----");
    }
    void Present() override {
        SA_LOG_INFO("[DEV] Present");
    }
    void WaitIdle() override {
        SA_LOG_INFO("[DEV] WaitIdle");
    }

    // ── Introspection ───────────────────────────────────────────────────────────

    const RHITextureDesc* GetTextureDesc(RHITextureHandle) const override {
        return nullptr;  // Null device stores no descs
    }
    RHIMemoryStats GetMemoryStats() const override {
        return RHIMemoryStats{};
    }
    std::string_view GetDeviceName() const override {
        return "NullRHIDevice";
    }

    // ── Swapchain ─────────────────────────────────────────────────────────────

    RHITextureHandle GetSwapchainTexture() override {
        // Return a fixed fake handle representing the back-buffer
        return RHITextureHandle{0xFFFF0000u};
    }
    RHIFormat GetSwapchainFormat() override {
        return RHIFormat::BGRA8_UNORM;
    }
    uint32_t GetSwapchainWidth()  override { return m_swapchainWidth; }
    uint32_t GetSwapchainHeight() override { return m_swapchainHeight; }
    void ResizeSwapchain(uint32_t width, uint32_t height) override {
        m_swapchainWidth  = width;
        m_swapchainHeight = height;
        SA_LOG_INFO("[DEV] ResizeSwapchain  {}x{}", width, height);
    }

private:
    uint32_t m_swapchainWidth  = 1280;
    uint32_t m_swapchainHeight = 720;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — build hand-written ShaderReflections that mirror what
// spirv-reflect would produce from the geometry pass shaders.
// ─────────────────────────────────────────────────────────────────────────────
static ShaderReflection MakeGeometryVertReflection() {
    // geometry.vert uses:
    //   set=0, binding=0 — CameraUBO   (uniform buffer, vertex stage)
    // push constants: 128 bytes (ModelMatrix + NormalMatrix), vertex stage
    ShaderReflection r;
    r.bindings.push_back({
        .set       = 0,
        .binding   = 0,
        .type      = RHIDescriptorType::UniformBuffer,
        .stages    = RHIShaderStage::Vertex,
        .name      = "CameraUBO",
    });
    r.pushConstantSize   = 128;
    r.pushConstantStages = RHIShaderStage::Vertex;
    return r;
}

static ShaderReflection MakeGeometryFragReflection() {
    // geometry.frag uses:
    //   set=0, binding=0 — CameraUBO   (also read in frag for world-space ops)
    //   set=1, binding=0 — u_AlbedoMap (texture, frag stage)
    //   set=1, binding=1 — u_NormalMap (texture, frag stage)
    //   set=1, binding=2 — u_Sampler   (sampler, frag stage)
    //   set=1, binding=3 — MaterialUBO (uniform buffer, frag stage)
    ShaderReflection r;
    r.bindings.push_back({0, 0, RHIDescriptorType::UniformBuffer,  RHIShaderStage::Fragment, "CameraUBO"});
    r.bindings.push_back({1, 0, RHIDescriptorType::Texture2D,      RHIShaderStage::Fragment, "u_AlbedoMap"});
    r.bindings.push_back({1, 1, RHIDescriptorType::Texture2D,      RHIShaderStage::Fragment, "u_NormalMap"});
    r.bindings.push_back({1, 2, RHIDescriptorType::Sampler,        RHIShaderStage::Fragment, "u_Sampler"});
    r.bindings.push_back({1, 3, RHIDescriptorType::UniformBuffer,  RHIShaderStage::Fragment, "MaterialUBO"});
    r.pushConstantSize   = 0;
    r.pushConstantStages = RHIShaderStage::None;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Demo entry point
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== RHI Interface Demo (Stage 1) ===");

    // ── 1. Instantiate device ─────────────────────────────────────────────────
    NullRHIDevice device;

    // ── 2. Validate Handle type-safety ───────────────────────────────────────
    {
        SA_LOG_INFO("--- Handle<T> validation ---");
        RHITextureHandle tex{42};
        RHIBufferHandle  buf{42};    // Same index, different type
        assert(tex.IsValid());
        assert(buf.IsValid());
        // tex == buf would be a compile error — confirmed type-safe
        assert(tex.index == buf.index);  // Values equal, but types are distinct
        SA_LOG_INFO("Handle type-safety: PASS (same index, incompatible types at compile time)");

        RHITextureHandle invalid{};
        assert(!invalid.IsValid());
        assert(!static_cast<bool>(invalid));
        SA_LOG_INFO("Handle invalid sentinel: PASS");
    }

    // ── 3. Create GPU resources ───────────────────────────────────────────────
    SA_LOG_INFO("--- Resource creation ---");

    RHITextureHandle albedoTex = device.CreateTexture({
        .width = 1024, .height = 1024,
        .format = RHIFormat::RGBA8_SRGB,
        .usage  = RHITextureUsage::Sampled | RHITextureUsage::CopyDst,
        .debugName = "Albedo_Sphere",
    });

    RHITextureHandle normalTex = device.CreateTexture({
        .width = 1024, .height = 1024,
        .format = RHIFormat::RG16F,
        .usage  = RHITextureUsage::Sampled | RHITextureUsage::CopyDst,
        .debugName = "Normal_Sphere",
    });

    RHITextureHandle gbufferColor = device.CreateTexture({
        .width = 1920, .height = 1080,
        .format = RHIFormat::RGBA8_UNORM,
        .usage  = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled,
        .debugName = "GBuffer_Albedo",
    });

    RHITextureHandle gbufferDepth = device.CreateTexture({
        .width = 1920, .height = 1080,
        .format = RHIFormat::D32F,
        .usage  = RHITextureUsage::DepthStencil | RHITextureUsage::Sampled,
        .debugName = "GBuffer_Depth",
    });

    RHIBufferHandle cameraUBO = device.CreateBuffer({
        .size      = 256,
        .usage     = RHIBufferUsage::Uniform,
        .cpuVisible = true,
        .debugName  = "CameraUBO",
    });

    RHIBufferHandle materialUBO = device.CreateBuffer({
        .size      = 64,
        .usage     = RHIBufferUsage::Uniform,
        .cpuVisible = true,
        .debugName  = "MaterialUBO",
    });

    RHIBufferHandle vertexBuffer = device.CreateBuffer({
        .size      = 1024 * sizeof(float) * 8,
        .usage     = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDst,
        .debugName = "Sphere_VB",
    });

    RHIBufferHandle indexBuffer = device.CreateBuffer({
        .size      = 4096 * sizeof(uint32_t),
        .usage     = RHIBufferUsage::Index | RHIBufferUsage::CopyDst,
        .debugName = "Sphere_IB",
    });

    // ── 4. Build shader reflections (as if loaded from .refl files) ───────────
    SA_LOG_INFO("--- Shader reflection ---");

    ShaderReflection vertRefl = MakeGeometryVertReflection();
    ShaderReflection fragRefl = MakeGeometryFragReflection();

    // Fake SPIR-V bytes (the NullDevice ignores them)
    const std::array<uint8_t, 4> dummySPIRV = {0x03, 0x02, 0x23, 0x07};

    RHIShaderHandle vertShader = device.CreateShader(dummySPIRV, vertRefl);
    RHIShaderHandle fragShader = device.CreateShader(dummySPIRV, fragRefl);

    // ── 5. Merge reflections → auto-derive layouts ────────────────────────────
    SA_LOG_INFO("--- Reflection merge & layout creation ---");

    ShaderReflection merged = MergeReflections(vertRefl, fragRefl);

    // Verify merge: CameraUBO should now carry Vertex | Fragment
    {
        auto cameraBinding = merged.FindBinding("CameraUBO");
        assert(cameraBinding.has_value());
        assert(HasStage(cameraBinding->stages, RHIShaderStage::Vertex));
        assert(HasStage(cameraBinding->stages, RHIShaderStage::Fragment));
        SA_LOG_INFO("Reflection merge: CameraUBO stage union PASS (Vertex|Fragment)");
    }

    // Verify push constant merge: vert has 128 bytes, frag has 0 → result 128
    assert(merged.pushConstantSize == 128);
    SA_LOG_INFO("Reflection merge: pushConstantSize={} PASS", merged.pushConstantSize);

    // Build descriptor set layouts from merged reflection
    RHIDescLayoutHandle set0Layout = device.CreateDescriptorSetLayout(merged, 0); // CameraUBO
    RHIDescLayoutHandle set1Layout = device.CreateDescriptorSetLayout(merged, 1); // Material textures

    // ── 6. Create pipeline ────────────────────────────────────────────────────
    SA_LOG_INFO("--- Pipeline creation ---");

    RHIPipelineDesc pipeDesc{};
    pipeDesc.vertShader              = vertShader;
    pipeDesc.fragShader              = fragShader;
    pipeDesc.descriptorLayouts[0]    = set0Layout;
    pipeDesc.descriptorLayouts[1]    = set1Layout;
    pipeDesc.descriptorLayoutCount   = 2;
    pipeDesc.pushConstantSize        = merged.pushConstantSize;
    pipeDesc.pushConstantStages      = merged.pushConstantStages;
    pipeDesc.colorFormats[0]         = RHIFormat::RGBA8_UNORM;
    pipeDesc.colorFormatCount        = 1;
    pipeDesc.depthFormat             = RHIFormat::D32F;
    RHIPipelineHandle geomPipeline = device.CreatePipeline(pipeDesc);

    // ── 7. Write descriptors using name-based binding lookup ──────────────────
    SA_LOG_INFO("--- Descriptor set writes ---");

    // set=0: camera
    RHIDescSetHandle cameraDS = device.AllocateDescriptorSet(set0Layout);
    {
        auto b = merged.FindBinding("CameraUBO");
        assert(b.has_value());
        device.WriteDescriptorBuffer(cameraDS, b->binding, cameraUBO);
    }

    // set=1: material
    RHIDescSetHandle materialDS = device.AllocateDescriptorSet(set1Layout);
    {
        auto albedoBinding   = merged.FindBinding("u_AlbedoMap");
        auto normalBinding   = merged.FindBinding("u_NormalMap");
        auto materialBinding = merged.FindBinding("MaterialUBO");
        assert(albedoBinding && normalBinding && materialBinding);

        device.WriteDescriptorTexture(materialDS, albedoBinding->binding,   albedoTex);
        device.WriteDescriptorTexture(materialDS, normalBinding->binding,    normalTex);
        device.WriteDescriptorBuffer (materialDS, materialBinding->binding,  materialUBO);
    }

    // ── 8. Simulate a frame ───────────────────────────────────────────────────
    SA_LOG_INFO("--- Frame simulation ---");

    IRHICommandList* cmd = device.BeginFrame();

    // Resource barriers: transition G-buffer attachments to their write states
    cmd->TransitionTexture(gbufferColor, RHIResourceState::Undefined,
                                         RHIResourceState::RenderTarget);
    cmd->TransitionTexture(gbufferDepth, RHIResourceState::Undefined,
                                         RHIResourceState::DepthWrite);
    // Albedo / normal textures already CopyDst; transition to ShaderRead
    cmd->TransitionTexture(albedoTex,    RHIResourceState::CopyDst,
                                         RHIResourceState::ShaderRead);
    cmd->TransitionTexture(normalTex,    RHIResourceState::CopyDst,
                                         RHIResourceState::ShaderRead);

    // Begin geometry render pass
    RHIRenderPassDesc rpDesc{};
    rpDesc.colorAttachments[0] = {.texture = gbufferColor, .clearOnLoad = true,
                                  .clearColor = {0.f, 0.f, 0.f, 1.f}};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.depthAttachment      = {.texture = gbufferDepth, .clearOnLoad = true};
    rpDesc.hasDepth             = true;
    rpDesc.width                = 1920;
    rpDesc.height               = 1080;
    cmd->BeginRenderPass(rpDesc);

    cmd->SetViewport({.x = 0, .y = 0, .width = 1920, .height = 1080});
    cmd->SetScissor ({.offsetX = 0, .offsetY = 0, .width = 1920, .height = 1080});
    cmd->SetPipeline(geomPipeline);
    cmd->SetDescriptorSet(0, cameraDS);
    cmd->SetDescriptorSet(1, materialDS);

    // Push constants: ModelMatrix (64 bytes) + NormalMatrix (64 bytes)
    struct PushData { float model[16]; float normalMat[16]; } push{};
    cmd->SetPushConstants(&push, sizeof(push), RHIShaderStage::Vertex);

    cmd->SetVertexBuffer(0, vertexBuffer);
    cmd->SetIndexBuffer(indexBuffer);
    cmd->DrawIndexed(/*indices=*/4096);

    cmd->EndRenderPass();

    // Transition G-buffer to ShaderRead for the lighting pass
    cmd->TransitionTexture(gbufferColor, RHIResourceState::RenderTarget,
                                         RHIResourceState::ShaderRead);
    cmd->TransitionTexture(gbufferDepth, RHIResourceState::DepthWrite,
                                         RHIResourceState::ShaderRead);

    device.EndFrame();
    device.Present();

    // ── 9. Window Surface & Swapchain interface validation ───────────────────
    // In production the RHIDeviceDesc is built from a real GLFWWindow:
    //
    //   auto win = GLFWWindow::Create({.width=1280, .height=720, .title="App"});
    //   RHIDeviceDesc deviceDesc{
    //       .windowHandle = {win->GetNativeHandle()},
    //       .swapchainWidth  = win->GetWidth(),
    //       .swapchainHeight = win->GetHeight(),
    //   };
    //   auto realDevice = VulkanDevice::Create(deviceDesc);
    //
    // Here we verify the null-device's swapchain API.
    SA_LOG_INFO("--- Swapchain interface ---");

    // Validate swapchain texture is a valid handle distinct from regular textures
    RHITextureHandle swapTex = device.GetSwapchainTexture();
    assert(swapTex.IsValid());
    SA_LOG_INFO("Swapchain texture handle: {}", swapTex.index);
    SA_LOG_INFO("Swapchain format: {}",  static_cast<uint32_t>(device.GetSwapchainFormat()));
    SA_LOG_INFO("Swapchain size:   {}x{}", device.GetSwapchainWidth(), device.GetSwapchainHeight());

    // Simulate window resize (OS callback → ResizeSwapchain)
    device.ResizeSwapchain(2560, 1440);
    assert(device.GetSwapchainWidth()  == 2560);
    assert(device.GetSwapchainHeight() == 1440);
    SA_LOG_INFO("Swapchain resize: PASS");

    // The swapchain texture can be imported into RenderGraph for the present pass:
    //   RGTextureHandle backbuffer = rg.ImportTexture(
    //       device.GetSwapchainTexture(), RHIResourceState::Present);

    // ── 9.5 Storage-image array binding (Issue #94 SPD infra) ─────────────────
    // Validates the reflection→layout→per-element-write path a SPD mip-chain
    // downsampler needs: a single storage-image ARRAY binding whose element i
    // targets mip i. Purely non-graphical — asserts on the null device.
    SA_LOG_INFO("--- Storage-image array (Issue #94) ---");
    {
        ShaderReflection spdRefl;
        spdRefl.bindings.push_back({
            .set       = 1,
            .binding   = 0,
            .type      = RHIDescriptorType::StorageImage,
            .stages    = RHIShaderStage::Compute,
            .name      = "u_mips",
            .arraySize = 8,   // image2D u_mips[8] — SPD writes up to 12+1 mips
        });

        // Reflection carries the array size through unchanged.
        auto mipsBinding = spdRefl.FindBinding("u_mips");
        assert(mipsBinding.has_value());
        assert(mipsBinding->arraySize == 8);
        assert(mipsBinding->type == RHIDescriptorType::StorageImage);
        SA_LOG_INFO("Reflection: u_mips arraySize={} PASS", mipsBinding->arraySize);

        RHIDescLayoutHandle spdLayout = device.CreateDescriptorSetLayout(spdRefl, 1);
        RHIDescSetHandle    spdDS     = device.AllocateDescriptorSet(spdLayout);

        // Bind each mip as a distinct array element of the single binding.
        for (uint32_t mip = 0; mip < 8; ++mip)
            device.WriteDescriptorStorageImageArrayMip(spdDS, mipsBinding->binding,
                                                       /*arrayElement=*/mip, albedoTex, mip);

        assert(device.m_storageArrayWriteCount == 8);
        SA_LOG_INFO("Storage-image array writes: {} elements PASS",
                    device.m_storageArrayWriteCount);
    }

    // ── 10. Teardown ──────────────────────────────────────────────────────────
    SA_LOG_INFO("--- Teardown ---");
    device.WaitIdle();
    device.DestroyPipeline(geomPipeline);
    device.DestroyShader(vertShader);
    device.DestroyShader(fragShader);
    device.DestroyBuffer(cameraUBO);
    device.DestroyBuffer(materialUBO);
    device.DestroyBuffer(vertexBuffer);
    device.DestroyBuffer(indexBuffer);
    device.DestroyTexture(albedoTex);
    device.DestroyTexture(normalTex);
    device.DestroyTexture(gbufferColor);
    device.DestroyTexture(gbufferDepth);

    SA_LOG_INFO("=== Stage 1 complete — all assertions passed ===");
    // Stage 2 will add: RenderGraph (virtual resources, pass declarations,
    //   compile() topology sort + barrier inference, execute())
    Core::Log::Shutdown();
    return 0;
}
