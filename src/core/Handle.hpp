#pragma once

#include <cstdint>
#include <limits>

namespace StellarAlia::Core {

/**
 * Strong-typed opaque handle wrapping a uint32 index.
 * Each distinct Tag yields an incompatible type — prevents passing a
 * TextureHandle where a BufferHandle is expected.
 *
 * Usage:
 *   using RHITextureHandle = Handle<struct RHITextureTag>;
 *   using RHIBufferHandle  = Handle<struct RHIBufferTag>;
 */
template<typename Tag>
struct Handle {
    static constexpr uint32_t INVALID_INDEX = std::numeric_limits<uint32_t>::max();

    uint32_t index = INVALID_INDEX;

    [[nodiscard]] bool IsValid() const noexcept { return index != INVALID_INDEX; }
    explicit operator bool() const noexcept { return IsValid(); }

    bool operator==(const Handle&) const noexcept = default;
    bool operator!=(const Handle&) const noexcept = default;
};

} // namespace StellarAlia::Core
