#include "function/ibl/GpuLtcBake.hpp"
#include "function/ibl/LtcLut.hpp"
#include "core/logs/Log.hpp"

#include <cassert>

namespace StellarAlia {

void GpuLtcBake::Upload(RHI::IRHIDevice* device) {
    assert(device);

    RHI::RHITextureDesc td{};
    td.width  = LUT_SIZE;
    td.height = LUT_SIZE;
    td.format = RHI::RHIFormat::RGBA32F;
    td.usage  = RHI::RHITextureUsage::Sampled;

    td.debugName = "LtcMat";
    m_ltcMat = device->CreateTexture(td);
    device->UploadTextureData(m_ltcMat, LTC1, sizeof(LTC1));

    td.debugName = "LtcAmp";
    m_ltcAmp = device->CreateTexture(td);
    device->UploadTextureData(m_ltcAmp, LTC2, sizeof(LTC2));

    SA_LOG_INFO("GpuLtcBake: uploaded LTC LUTs ({}x{} RGBA32F)", LUT_SIZE, LUT_SIZE);
}

void GpuLtcBake::Shutdown(RHI::IRHIDevice* device) {
    if (!device) return;
    if (m_ltcMat.IsValid()) device->DestroyTexture(m_ltcMat);
    if (m_ltcAmp.IsValid()) device->DestroyTexture(m_ltcAmp);
    m_ltcMat = {};
    m_ltcAmp = {};
}

} // namespace StellarAlia
