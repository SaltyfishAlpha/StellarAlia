#pragma once

// LTC lookup table arrays, defined in LtcLut.cpp.
// Each table is 64×64 texels × 4 floats (RGBA32F).
// Source: Heitz et al. 2016 "Real-Time Polygonal-Light Shading with LTC"
//
// LTC1[i*4 + {0,1,2,3}] = { m00, m02, m11, m20 }  (packed inverse-M matrix)
// LTC2[i*4 + {0,1,2,3}] = { GGX norm, Fresnel, sphere, 0 }

namespace StellarAlia {

constexpr int LTC_LUT_SIZE    = 64;
constexpr int LTC_LUT_FLOATS  = LTC_LUT_SIZE * LTC_LUT_SIZE * 4;  // 16384

extern const float LTC1[LTC_LUT_FLOATS];
extern const float LTC2[LTC_LUT_FLOATS];

} // namespace StellarAlia
