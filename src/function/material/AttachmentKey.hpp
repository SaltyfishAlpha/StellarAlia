#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

#include "platform/rhi/IRHIDevice.hpp"
#include "platform/rhi/RHITypes.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// AttachmentKey — identifies the render target formats of a render pass.
// Used as a hash key in ShaderProgram's pipeline cache.
// ─────────────────────────────────────────────────────────────────────────────
struct AttachmentKey {
    RHI::RHIFormat colorFormats[4] = {};
    uint32_t       colorCount      = 0;
    RHI::RHIFormat depthFormat     = RHI::RHIFormat::Undefined;

    bool operator==(const AttachmentKey& o) const noexcept {
        if (colorCount != o.colorCount || depthFormat != o.depthFormat) return false;
        for (uint32_t i = 0; i < colorCount; ++i)
            if (colorFormats[i] != o.colorFormats[i]) return false;
        return true;
    }
};

struct AttachmentKeyHash {
    size_t operator()(const AttachmentKey& k) const noexcept {
        size_t h = std::hash<uint32_t>{}(k.colorCount)
                 ^ (std::hash<uint32_t>{}(static_cast<uint32_t>(k.depthFormat)) << 16);
        for (uint32_t i = 0; i < k.colorCount; ++i)
            h ^= std::hash<uint32_t>{}(static_cast<uint32_t>(k.colorFormats[i])) << (i * 4);
        return h;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Issue #56 — full fixed-function render state. Together with AttachmentKey it
// identifies one pipeline permutation of a ShaderProgram: the same shader can
// now serve {Opaque/Mask}×{single/double-sided}×{LEQUAL/EQUAL}×{stencil...}.
// ─────────────────────────────────────────────────────────────────────────────
struct PipelineRenderState {
    RHI::RHICullMode       cullMode           = RHI::RHICullMode::Back;
    RHI::RHIBlendMode      blendMode          = RHI::RHIBlendMode::Opaque;
    RHI::RHITopology       topology           = RHI::RHITopology::TriangleList;
    RHI::RHICompareOp      depthCompareOp     = RHI::RHICompareOp::LessOrEqual;
    bool                   depthTest          = true;
    bool                   depthWrite         = true;
    bool                   noVertexInput      = false;
    bool                   stencilTestEnable  = false;
    bool                   stencilWriteEnable = false;
    RHI::RHIStencilOpState stencilFront;
    RHI::RHIStencilOpState stencilBack;

    bool operator==(const PipelineRenderState&) const noexcept = default;
};

struct PipelineStateKey {
    AttachmentKey       attachments;
    PipelineRenderState state;

    bool operator==(const PipelineStateKey&) const noexcept = default;
};

struct PipelineStateKeyHash {
    size_t operator()(const PipelineStateKey& k) const noexcept {
        size_t h = AttachmentKeyHash{}(k.attachments);
        const auto& s = k.state;
        // compareMask/writeMask omitted (constant 0xFF in practice) — equality
        // still checks them, a hash collision there is merely a probe.
        const uint64_t bits =
              (uint64_t(s.cullMode))
            | (uint64_t(s.blendMode)                 << 2)
            | (uint64_t(s.topology)                  << 4)
            | (uint64_t(s.depthCompareOp)            << 6)
            | (uint64_t(s.depthTest)                 << 9)
            | (uint64_t(s.depthWrite)                << 10)
            | (uint64_t(s.noVertexInput)             << 11)
            | (uint64_t(s.stencilTestEnable)         << 12)
            | (uint64_t(s.stencilWriteEnable)        << 13)
            | (uint64_t(s.stencilFront.compareOp)    << 14)
            | (uint64_t(s.stencilFront.passOp)       << 17)
            | (uint64_t(s.stencilFront.failOp)       << 20)
            | (uint64_t(s.stencilFront.reference)    << 23)
            | (uint64_t(s.stencilBack.compareOp)     << 31)
            | (uint64_t(s.stencilBack.passOp)        << 34)
            | (uint64_t(s.stencilBack.failOp)        << 37)
            | (uint64_t(s.stencilBack.reference)     << 40);
        return h ^ (std::hash<uint64_t>{}(bits) << 1);
    }
};

} // namespace StellarAlia
