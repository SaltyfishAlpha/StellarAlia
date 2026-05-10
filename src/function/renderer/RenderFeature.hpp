#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "function/render_graph/RenderGraph.hpp"
#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia {

class MaterialManager;
class SceneRenderer;
namespace Resource { class ResourceManager; }

// ─────────────────────────────────────────────────────────────────────────────
// FeatureInitContext — all initialisation-time dependencies in one place.
// ─────────────────────────────────────────────────────────────────────────────
struct FeatureInitContext {
    RHI::IRHIDevice*           device      = nullptr;
    MaterialManager*           matMgr      = nullptr;
    Resource::ResourceManager* resMgr      = nullptr;
    RHI::RHIDescLayoutHandle   frameLayout;
    std::string                shaderDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// RendererHandles — RG handles for all built-in render targets.
//
// Passed to RenderFeature::AddPasses every frame. Use these handles to:
//   • Declare dependencies: b.Read(handles.hdr), b.Write(handles.swapchain)
//   • Bind to descriptor sets: ctx.BindTexture(set, binding, handles.hdr)
//
// Fields are only valid for the duration of AddPasses(). Do not cache.
// ─────────────────────────────────────────────────────────────────────────────
struct RendererHandles {
    // ── Primary buffers ───────────────────────────────────────────────────────
    RGTextureHandle hdr;          // RGBA16F  HDR target: raw lighting → BloomComposite writes here → Tonemap reads
    RGTextureHandle taaResolved;  // RGBA16F  TAA history output (= hdr when TAA disabled); Bloom threshold reads this
    RGTextureHandle swapchain;    // swapchain colour attachment
    RGTextureHandle depth;        // D32F     scene depth

    // ── G-Buffer ──────────────────────────────────────────────────────────────
    RGTextureHandle gbufferRT0;   // RGBA8_UNORM  albedo.rgb + occlusion.a
    RGTextureHandle gbufferRT1;   // RGBA16F      oct-normal + roughness + metallic
    RGTextureHandle gbufferRT2;   // RGBA16F      emissive.rgb

    // ── Shadow map ────────────────────────────────────────────────────────────
    RGTextureHandle shadowMap;    // D32F  (invalid handle when shadow disabled)

    // ── Bloom pyramid ─────────────────────────────────────────────────────────
    int             bloomMipCount = 0;
    RGTextureHandle bloomMip[8];  // [0]=1/2 … [n-1]=1/(2^n) resolution

    // ── Selection outline mask + intermediate ─────────────────────────────────
    RGTextureHandle selectionMask; // R8_UNORM  1-bit per-pixel coverage mask
    RGTextureHandle dilateH;       // R8_UNORM  horizontally-dilated mask (separable pass 1)

    // ── SSAO (GTAO) result ────────────────────────────────────────────────────
    RGTextureHandle ssaoTex;       // R8_UNORM  final blurred AO (1.0 = no occlusion)
};

// ─────────────────────────────────────────────────────────────────────────────
// FrameContext — per-frame services for RenderFeature::AddPasses.
//
// Exposes the RenderGraph, the frame descriptor set, and texture binding —
// without leaking raw RHITextureHandle for renderer-internal buffers.
//
// Usage pattern for a custom tonemap feature:
//
//   // OnInit:
//   m_descSet = ctx.device->AllocateDescriptorSet(layout);   // 1 sampler2D
//
//   // AddPasses:
//   if (resized) ctx.BindTexture(m_descSet, 0, handles.hdr); // no RHI handle
//   ctx.rg->AddPass("MyTonemap",
//       [&handles](RGPassBuilder& b){ b.Read(handles.hdr); b.Write(handles.swapchain); },
//       [=](IRHICommandList& cmd, const RGResources& res){ ... });
// ─────────────────────────────────────────────────────────────────────────────
struct FrameContext {
    // ── RenderGraph for this frame ────────────────────────────────────────────
    RenderGraph*          rg      = nullptr;

    // ── set=0 descriptor set (camera, lights, IBL LUTs) ──────────────────────
    RHI::RHIDescSetHandle frameSet;

    // ── Device (pipeline creation, external resource management) ─────────────
    RHI::IRHIDevice*      device  = nullptr;

    // ── Texture binding without exposing RHITextureHandle ────────────────────
    //
    // Queues a descriptor write that is resolved and flushed after Execute()
    // when all physical slot handles are valid (imported and transient alike).
    // No-op if either handle is invalid.
    void BindTexture(RHI::RHIDescSetHandle set, uint32_t binding,
                     RGTextureHandle handle) const;

    // ── Buffer binding without exposing RHIBufferHandle ───────────────────────
    //
    // Queues a descriptor write (SSBO / uniform buffer) resolved after Execute().
    // No-op if either handle is invalid.
    void BindBuffer(RHI::RHIDescSetHandle set, uint32_t binding,
                    RGBufferHandle handle) const;

private:
    friend class SceneRenderer;

    struct PendingBinding {
        RHI::RHIDescSetHandle set;
        uint32_t              binding;
        RGTextureHandle       handle;
    };
    struct PendingBufferBinding {
        RHI::RHIDescSetHandle set;
        uint32_t              binding;
        RGBufferHandle        handle;
    };
    // mutable: Bind* methods are const so callers need not change const FrameContext& signatures.
    mutable std::vector<PendingBinding>       m_pendingBindings;
    mutable std::vector<PendingBufferBinding> m_pendingBufferBindings;

    // Resolves all queued bindings and writes descriptor sets.
    // Called by SceneRenderer after AllocateSlots() but before Execute().
    void FlushBindings() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// RenderFeature
// ─────────────────────────────────────────────────────────────────────────────
class RenderFeature {
public:
    virtual ~RenderFeature() = default;

    virtual void OnInit(const FeatureInitContext& /*ctx*/) {}
    virtual void OnShutdown(RHI::IRHIDevice* /*device*/) {}

    // Called once per frame inside SceneRenderer::RenderFrame.
    //
    // renderer — built-in (nested) features use it to access private members.
    //            External user features should ignore it; private members are
    //            not accessible from outside SceneRenderer anyway.
    // ctx      — RG, frameSet, device, BindTexture(). Use this for RG injection
    //            and descriptor set writes.
    // handles  — RGTextureHandle for every built-in render target. Use these
    //            for b.Read/Write declarations and ctx.BindTexture().
    virtual void AddPasses(SceneRenderer&         renderer,
                           const FrameContext&    ctx,
                           const RendererHandles& handles,
                           const entt::registry&  reg,
                           uint32_t               width,
                           uint32_t               height) = 0;
};

} // namespace StellarAlia
