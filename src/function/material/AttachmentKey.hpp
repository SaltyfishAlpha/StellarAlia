#pragma once

#include <cstdint>
#include <cstring>
#include <functional>

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

} // namespace StellarAlia
