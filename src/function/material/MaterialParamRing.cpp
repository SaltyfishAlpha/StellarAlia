#include "function/material/MaterialParamRing.hpp"

#include <algorithm>

#include "core/logs/Log.hpp"

namespace StellarAlia {

bool MaterialParamRing::Init(RHI::IRHIDevice* device, uint64_t bytesPerFrame) {
    m_device    = device;
    m_capacity  = bytesPerFrame;
    m_offset    = 0;
    m_alignment = std::max(1u, device->GetMinStorageBufferOffsetAlignment());

    RHI::RHIBufferDesc desc{};
    desc.size       = m_capacity;
    desc.usage      = RHI::RHIBufferUsage::Storage;
    desc.cpuVisible = true;  // persistently mapped via VMA
    desc.debugName  = "MaterialParamRing";
    m_buf = device->CreateBuffer(desc);
    if (!m_buf.IsValid()) {
        SA_LOG_ERROR("MaterialParamRing::Init — CreateBuffer failed ({} bytes)", m_capacity);
        m_device = nullptr;
        return false;
    }
    SA_LOG_INFO("MaterialParamRing: {} KiB ring, alignment={} B",
                m_capacity / 1024, m_alignment);
    return true;
}

void MaterialParamRing::Shutdown() {
    if (m_device && m_buf.IsValid())
        m_device->DestroyBuffer(m_buf);
    m_buf      = {};
    m_device   = nullptr;
    m_capacity = 0;
    m_offset   = 0;
}

void MaterialParamRing::Reset() {
    m_offset = 0;
}

uint32_t MaterialParamRing::Allocate(const void* blob, uint32_t size) {
    if (!m_device || !m_buf.IsValid() || size == 0) return kInvalidOffset;
    const uint64_t aligned = (m_offset + m_alignment - 1) & ~static_cast<uint64_t>(m_alignment - 1);
    if (aligned + size > m_capacity) {
        SA_LOG_ERROR("MaterialParamRing: capacity {} B exhausted "
                     "(requested {} B at offset {}); material params not uploaded",
                     m_capacity, size, aligned);
        return kInvalidOffset;
    }
    m_device->UploadBufferData(m_buf, blob, size, aligned);
    m_offset = aligned + size;
    return static_cast<uint32_t>(aligned);
}

} // namespace StellarAlia
