#pragma once

#include "platform/rhi/IRHIDevice.hpp"

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// GpuLtcBake
//
// Uploads the embedded LTC (Linearly Transformed Cosines) lookup tables as
// 64×64 RGBA32F GPU textures.  The raw float data lives in LtcLut.cpp.
//
// LTC1 — inverse-M matrix LUT   → bound as set=0 binding=5 (t_LtcMat)
// LTC2 — amplitude/GGX-norm LUT → bound as set=0 binding=6 (t_LtcAmp)
//
// Usage:
//   GpuLtcBake ltc;
//   ltc.Upload(device);
//   fub.SetLtcTextures(ltc.GetLtcMat(), ltc.GetLtcAmp());
// ─────────────────────────────────────────────────────────────────────────────
class GpuLtcBake {
public:
    static constexpr int LUT_SIZE = 64;  // 64×64 texels

    // Allocates and fills the two GPU textures from the embedded arrays.
    // Must be called inside an ImmediateSubmit / ImmediateCompute block or
    // any context where UploadTextureData is valid.
    void Upload(RHI::IRHIDevice* device);

    void Shutdown(RHI::IRHIDevice* device);

    [[nodiscard]] RHI::RHITextureHandle GetLtcMat() const { return m_ltcMat; }
    [[nodiscard]] RHI::RHITextureHandle GetLtcAmp() const { return m_ltcAmp; }
    [[nodiscard]] bool IsUploaded() const { return m_ltcMat.IsValid(); }

private:
    RHI::RHITextureHandle m_ltcMat;
    RHI::RHITextureHandle m_ltcAmp;
};

} // namespace StellarAlia
