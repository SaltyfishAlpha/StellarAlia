#pragma once

#include <cstdint>

#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialParamRing
//
// Per-frame bump allocator backed by a single cpu-visible SSBO. Each draw call's
// material parameter blob is memcpy'd in and the returned offset is passed as
// the dynamic offset of a `STORAGE_BUFFER_DYNAMIC` descriptor. Reset() must be
// called once per frame before any Allocate().
//
// Capacity exhaustion is fail-loud: Allocate() returns kInvalidOffset and logs
// an error. Callers must handle (e.g. skip the draw).
// ─────────────────────────────────────────────────────────────────────────────
class MaterialParamRing {
public:
    static constexpr uint32_t kInvalidOffset = 0xFFFFFFFFu;

    bool Init(RHI::IRHIDevice* device, uint64_t bytesPerFrame = 2u * 1024u * 1024u);
    void Shutdown();

    void Reset();
    uint32_t Allocate(const void* blob, uint32_t size);

    [[nodiscard]] RHI::RHIBufferHandle GetBuffer()    const { return m_buf; }
    [[nodiscard]] uint32_t             GetAlignment() const { return m_alignment; }
    [[nodiscard]] uint64_t             GetCapacity()  const { return m_capacity; }
    [[nodiscard]] uint64_t             GetUsedBytes() const { return m_offset; }

private:
    RHI::IRHIDevice*     m_device    = nullptr;
    RHI::RHIBufferHandle m_buf;
    uint64_t             m_capacity  = 0;
    uint64_t             m_offset    = 0;
    uint32_t             m_alignment = 16;
};

} // namespace StellarAlia
