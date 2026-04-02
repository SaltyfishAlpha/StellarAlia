#include "function/ibl/SHProjection.hpp"

#include <cmath>

namespace StellarAlia::IBL {

void ProjectHDRtoSH(const Resource::ImageData& hdr, glm::vec4 outSH[9]) {
    constexpr float kPI = 3.14159265359f;

    // Lambertian convolution factors per SH band (Ramamoorthi & Hanrahan 2001)
    const float kConv[9] = {
        kPI,                        // l=0 (1 coeff)
        2.0f * kPI / 3.0f,          // l=1 (3 coeffs)
        2.0f * kPI / 3.0f,
        2.0f * kPI / 3.0f,
        kPI / 4.0f,                 // l=2 (5 coeffs)
        kPI / 4.0f,
        kPI / 4.0f,
        kPI / 4.0f,
        kPI / 4.0f,
    };

    glm::vec3 accum[9] = {};
    double    weightSum = 0.0;

    const uint32_t W = hdr.width;
    const uint32_t H = hdr.height;

    for (uint32_t y = 0; y < H; ++y) {
        // Elevation: v=0 → top (+90°), v=1 → bottom (−90°)
        const float v    = (y + 0.5f) / static_cast<float>(H);
        const float elev = (0.5f - v) * kPI;   // −π/2 .. +π/2
        const float cosE = std::cos(elev);
        const float sinE = std::sin(elev);
        // Solid-angle weight for equirectangular: dΩ ∝ cos(elevation)
        const float latW = cosE;

        for (uint32_t x = 0; x < W; ++x) {
            const float u   = (x + 0.5f) / static_cast<float>(W);
            const float phi = (u * 2.0f - 1.0f) * kPI;   // −π .. +π

            // World direction (right-hand Y-up, same as EquirectToDir in shaders)
            const glm::vec3 d(cosE * std::cos(phi), sinE, cosE * std::sin(phi));

            // Pixel radiance (RGBA32F, 4 floats per pixel)
            const float* p = hdr.pixelsHDR.data() + (y * W + x) * 4;
            const glm::vec3 L(p[0], p[1], p[2]);

            // Real SH basis Y_i(d), l=0,1,2
            const float Y[9] = {
                0.282095f,
                0.488603f * d.y,
                0.488603f * d.z,
                0.488603f * d.x,
                1.092548f * d.x * d.y,
                1.092548f * d.y * d.z,
                0.315392f * (3.0f * d.z * d.z - 1.0f),
                1.092548f * d.x * d.z,
                0.546274f * (d.x * d.x - d.y * d.y),
            };

            weightSum += static_cast<double>(latW);
            for (int i = 0; i < 9; ++i)
                accum[i] += L * (Y[i] * latW);
        }
    }

    // Normalise: scale = 4π / Σweights, then apply Lambertian convolution
    const float scale = (weightSum > 0.0)
        ? static_cast<float>(4.0 * static_cast<double>(kPI) / weightSum)
        : 0.0f;

    for (int i = 0; i < 9; ++i)
        outSH[i] = glm::vec4(accum[i] * (scale * kConv[i]), 0.0f);
}

} // namespace StellarAlia::IBL
