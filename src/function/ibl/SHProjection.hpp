#pragma once

#include "resource/types/ImageData.hpp"
#include <glm/glm.hpp>

namespace StellarAlia::IBL {

// Projects an HDR equirectangular panorama into L0+L1+L2 real spherical
// harmonics (9 RGB coefficients).  Each coefficient is pre-multiplied by the
// Lambertian convolution kernel (Ramamoorthi & Hanrahan 2001, Table 1):
//   l=0: π      l=1: 2π/3 (×3)     l=2: π/4 (×5)
//
// The resulting outSH[9] can be passed directly to the SH irradiance evaluator
// in frame_uniforms.glsl (EvaluateSHIrradiance) — no further scaling needed.
// The w component of each vec4 is always 0 (std140 padding).
//
// Input: RGBA32F equirectangular image (hdr.isHDR must be true).
void ProjectHDRtoSH(const Resource::ImageData& hdr, glm::vec4 outSH[9]);

} // namespace StellarAlia::IBL
