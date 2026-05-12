#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// BindlessTextureHeap
//
// Global, fixed-size descriptor array of sampler2D (set=3 binding=0). Every
// texture used by any material shader registers here once at asset-load time
// and obtains a 32-bit index. Shaders sample via:
//   layout(set=3, binding=0) uniform sampler2D globalTex[];
//   texture(globalTex[nonuniformEXT(mat.t_BaseColor_Idx)], uv)
//
// MaterialOverrideComponent texture overrides are realised by writing a
// different index into the per-entity material SSBO blob — no per-entity
// descriptor allocation needed.
//
// Capacity is fixed at construction. Slot 0 is always the default texture
// (1×1 white) so kInvalid sampling never produces undefined behaviour.
// ─────────────────────────────────────────────────────────────────────────────
class BindlessTextureHeap {
public:
    static constexpr uint32_t kDefaultCapacity = 4096;
    static constexpr uint32_t kInvalid         = 0xFFFFFFFFu;
    static constexpr uint32_t kDefaultSlot     = 0;          // always default texture

    bool Init(RHI::IRHIDevice* device,
              RHI::RHITextureHandle defaultTexture,
              uint32_t capacity = kDefaultCapacity);
    void Shutdown();

    // Returns a slot index in [0, capacity). Returns kDefaultSlot on overflow
    // (with SA_LOG_ERROR) so the calling shader at least samples white.
    uint32_t Register(RHI::RHITextureHandle tex);

    // Returns slot to the free list. Slot 0 (default) is never freed.
    void Release(uint32_t slot);

    [[nodiscard]] RHI::RHIDescLayoutHandle GetLayout() const  { return m_layout; }
    [[nodiscard]] RHI::RHIDescSetHandle    GetDescSet() const { return m_descSet; }
    [[nodiscard]] uint32_t                 GetCapacity() const { return m_capacity; }

private:
    RHI::IRHIDevice*          m_device   = nullptr;
    RHI::RHIDescLayoutHandle  m_layout;
    RHI::RHIDescSetHandle     m_descSet;
    uint32_t                  m_capacity = 0;
    uint32_t                  m_nextSlot = 1;   // 0 reserved for default
    std::vector<uint32_t>     m_freeList;
    // Dedup: a given RHITextureHandle is registered at most once.
    std::unordered_map<uint32_t, uint32_t> m_texToSlot;  // tex.index → slot
    RHI::RHITextureHandle     m_defaultTexture;
};

} // namespace StellarAlia
