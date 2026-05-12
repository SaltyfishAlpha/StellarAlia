#include "function/material/BindlessTextureHeap.hpp"

#include "core/logs/Log.hpp"

namespace StellarAlia {

bool BindlessTextureHeap::Init(RHI::IRHIDevice* device,
                                RHI::RHITextureHandle defaultTexture,
                                uint32_t capacity) {
    m_device         = device;
    m_capacity       = capacity;
    m_nextSlot       = 1;     // slot 0 reserved
    m_defaultTexture = defaultTexture;
    m_freeList.clear();

    m_layout = device->CreateBindlessTextureLayout(capacity);
    if (!m_layout.IsValid()) {
        SA_LOG_ERROR("BindlessTextureHeap::Init — layout creation failed");
        return false;
    }
    m_descSet = device->AllocateDescriptorSet(m_layout);
    if (!m_descSet.IsValid()) {
        SA_LOG_ERROR("BindlessTextureHeap::Init — descriptor set allocation failed");
        return false;
    }

    // Fill slot 0 with the default texture so any kInvalid sample reads white
    // instead of garbage.
    if (defaultTexture.IsValid())
        device->WriteDescriptorTextureArray(m_descSet, 0, kDefaultSlot, defaultTexture);

    SA_LOG_INFO("BindlessTextureHeap: capacity={} slots, default at slot 0", capacity);
    return true;
}

void BindlessTextureHeap::Shutdown() {
    // Layout / descriptor set live in device-owned tables — not freed here.
    m_descSet   = {};
    m_layout    = {};
    m_capacity  = 0;
    m_nextSlot  = 1;
    m_freeList.clear();
    m_device    = nullptr;
}

uint32_t BindlessTextureHeap::Register(RHI::RHITextureHandle tex) {
    if (!m_device || !m_descSet.IsValid() || !tex.IsValid())
        return kDefaultSlot;

    // Dedup: same RHITextureHandle reuses the same slot. Avoids slot leakage
    // when a material asset references the same texture for multiple slots,
    // or when MaterialOverrideComponent updates resolve to the same tex.
    auto it = m_texToSlot.find(tex.index);
    if (it != m_texToSlot.end()) return it->second;

    uint32_t slot;
    if (!m_freeList.empty()) {
        slot = m_freeList.back();
        m_freeList.pop_back();
    } else if (m_nextSlot < m_capacity) {
        slot = m_nextSlot++;
    } else {
        SA_LOG_ERROR("BindlessTextureHeap: capacity {} exhausted; "
                     "texture falls back to default slot 0", m_capacity);
        return kDefaultSlot;
    }
    m_device->WriteDescriptorTextureArray(m_descSet, 0, slot, tex);
    m_texToSlot.emplace(tex.index, slot);
    return slot;
}

void BindlessTextureHeap::Release(uint32_t slot) {
    if (slot == kDefaultSlot || slot >= m_capacity) return;
    // Drop the dedup entry pointing at this slot so future Registers can
    // reclaim a fresh slot (otherwise the released slot would be invisible
    // to dedup but live in m_freeList).
    for (auto it = m_texToSlot.begin(); it != m_texToSlot.end(); ) {
        if (it->second == slot) it = m_texToSlot.erase(it);
        else                    ++it;
    }
    // Point freed slot back at the default texture so a stale shader index
    // still reads white rather than the previous occupant.
    if (m_device && m_defaultTexture.IsValid())
        m_device->WriteDescriptorTextureArray(m_descSet, 0, slot, m_defaultTexture);
    m_freeList.push_back(slot);
}

} // namespace StellarAlia
