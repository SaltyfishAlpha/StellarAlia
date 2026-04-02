// IblBake.cpp — CPU IBL precomputation
//
// All integration is done on the CPU using Riemann sums / importance sampling.
// This is intentionally single-threaded and accurate, not fast.
//
// Coordinate convention: right-hand Y-up (same as the rest of the engine).
// Equirectangular UV convention matches frame_uniforms.glsl DirToEquirect():
//   u ∈ [0,1] → longitude phi ∈ [-π, π]   (u=0 = -X direction)
//   v ∈ [0,1] → latitude  theta ∈ [π/2,-π/2] (v=0 = +Y pole)

#include "IblBake.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "core/logs/Log.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace StellarAlia::IblBake {

// ─── UUID derivation ──────────────────────────────────────────────────────────

static constexpr uint64_t kIrrSalt  = 0x0100000000000001ull;
static constexpr uint64_t kEnvSalt  = 0x0200000000000002ull;

IblAssetIDs DeriveIDs(const AssetID& hdrID) {
    IblAssetIDs ids;
    ids.irradiance  = { hdrID.hi ^ kIrrSalt, hdrID.lo ^ kIrrSalt };
    ids.prefiltered = { hdrID.hi ^ kEnvSalt, hdrID.lo ^ kEnvSalt };
    ids.brdfLut     = AssetID::FromString("c5b06992-5a8f-4dc9-9d11-406e12b969d4");
    return ids;
}

// ─── Equirectangular helpers ──────────────────────────────────────────────────

static glm::vec3 EquirectToDir(float u, float v) {
    float phi   = (u * 2.0f - 1.0f) * glm::pi<float>();
    float theta = (0.5f - v)        * glm::pi<float>();
    float ct    = std::cos(theta);
    return { ct * std::cos(phi), std::sin(theta), ct * std::sin(phi) };
}

static glm::vec2 DirToEquirect(glm::vec3 d) {
    float phi   = std::atan2(d.z, d.x);
    float theta = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
    return { phi / (2.0f * glm::pi<float>()) + 0.5f, 0.5f - theta / glm::pi<float>() };
}

// Bilinear sample from a float HDR panorama (RGBA interleaved, 4 floats/pixel).
static glm::vec3 SampleEquirect(const Resource::ImageData& hdr, glm::vec3 dir) {
    glm::vec2 uv = DirToEquirect(dir);
    // Wrap U, clamp V
    uv.x = uv.x - std::floor(uv.x);
    uv.y = glm::clamp(uv.y, 0.0f, 1.0f);

    float fx = uv.x * static_cast<float>(hdr.width  - 1);
    float fy = uv.y * static_cast<float>(hdr.height - 1);
    int   x0 = static_cast<int>(fx),  x1 = std::min(x0 + 1, static_cast<int>(hdr.width)  - 1);
    int   y0 = static_cast<int>(fy),  y1 = std::min(y0 + 1, static_cast<int>(hdr.height) - 1);
    float tx = fx - static_cast<float>(x0);
    float ty = fy - static_cast<float>(y0);

    auto fetch = [&](int x, int y) -> glm::vec3 {
        const float* p = hdr.pixelsHDR.data() + (y * static_cast<int>(hdr.width) + x) * 4;
        return { p[0], p[1], p[2] };
    };
    return glm::mix(glm::mix(fetch(x0, y0), fetch(x1, y0), tx),
                    glm::mix(fetch(x0, y1), fetch(x1, y1), tx), ty);
}

// ─── Sampling utilities ───────────────────────────────────────────────────────

// Van der Corput radical inverse
static float RadicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

static glm::vec2 Hammersley(uint32_t i, uint32_t N) {
    return { static_cast<float>(i) / static_cast<float>(N), RadicalInverse(i) };
}

// GGX importance sample — returns half-vector H in the hemisphere around N.
static glm::vec3 ImportanceSampleGGX(glm::vec2 Xi, glm::vec3 N, float roughness) {
    float a  = roughness * roughness;
    float phi      = 2.0f * glm::pi<float>() * Xi.x;
    float cosTheta = std::sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y + 1e-6f));
    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);

    glm::vec3 H = { sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta };

    glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 T  = glm::normalize(glm::cross(up, N));
    glm::vec3 B  = glm::cross(N, T);
    return glm::normalize(T * H.x + B * H.y + N * H.z);
}

// ─── Irradiance map ───────────────────────────────────────────────────────────
// Riemann-sum integral of L(ω)·cos(θ) dω over the hemisphere around each output N.

static Resource::CookedTexture BakeIrradiance(const Resource::ImageData& hdr,
                                               const AssetID& id) {
    constexpr uint32_t W       = 256;
    constexpr uint32_t H       = 128;
    constexpr uint32_t N_PHI   = 256;
    constexpr uint32_t N_THETA = 128;
    const float dPhi   = 2.0f * glm::pi<float>() / N_PHI;
    const float dTheta = 0.5f * glm::pi<float>() / N_THETA;

    std::vector<float> pixels(W * H * 4);

    for (uint32_t py = 0; py < H; ++py) {
        float v = (py + 0.5f) / H;
        for (uint32_t px = 0; px < W; ++px) {
            float u = (px + 0.5f) / W;
            glm::vec3 N = EquirectToDir(u, v);
            glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
            glm::vec3 T  = glm::normalize(glm::cross(up, N));
            glm::vec3 B  = glm::cross(N, T);

            glm::vec3 irr(0.0f);
            float wSum = 0.0f;

            for (uint32_t ti = 0; ti < N_THETA; ++ti) {
                float theta = (ti + 0.5f) * dTheta;
                float sinT  = std::sin(theta);
                float cosT  = std::cos(theta);
                float w     = cosT * sinT;  // NdotL × Jacobian
                for (uint32_t pi = 0; pi < N_PHI; ++pi) {
                    float phi = (pi + 0.5f) * dPhi;
                    glm::vec3 s = { sinT * std::cos(phi), sinT * std::sin(phi), cosT };
                    glm::vec3 sWorld = s.x * T + s.y * B + s.z * N;
                    irr  += SampleEquirect(hdr, sWorld) * w;
                    wSum += w;
                }
            }
            irr = (wSum > 0.0f) ? irr * (glm::pi<float>() / wSum) : glm::vec3(0.0f);

            float* dst = pixels.data() + (py * W + px) * 4;
            dst[0] = irr.r;  dst[1] = irr.g;  dst[2] = irr.b;  dst[3] = 1.0f;
        }
        if (py % 16 == 0)
            SA_LOG_INFO("  irradiance row {}/{}", py, H);
    }

    Resource::CookedTexture tex;
    tex.id        = id;
    tex.width     = W;
    tex.height    = H;
    tex.mipLevels = 1;
    tex.format    = Resource::CookedTextureFormat::RGBA32F;
    tex.srgb      = false;
    tex.isHDR     = true;
    uint64_t sz   = W * H * 4 * sizeof(float);
    tex.mips.push_back({ 0, sz });
    tex.data.resize(sz);
    std::memcpy(tex.data.data(), pixels.data(), sz);
    return tex;
}

// ─── Prefiltered env map ──────────────────────────────────────────────────────
// GGX importance sampling for roughness levels 0..1 across 5 mip levels.

static Resource::CookedTexture BakePrefiltered(const Resource::ImageData& hdr,
                                                const AssetID& id) {
    constexpr uint32_t BASE_W    = 512;
    constexpr uint32_t BASE_H    = 256;
    constexpr uint32_t NUM_MIPS  = 5;
    constexpr uint32_t N_SAMPLES = 1024;
    const float roughnesses[NUM_MIPS] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    Resource::CookedTexture tex;
    tex.id        = id;
    tex.width     = BASE_W;
    tex.height    = BASE_H;
    tex.mipLevels = NUM_MIPS;
    tex.format    = Resource::CookedTextureFormat::RGBA32F;
    tex.srgb      = false;
    tex.isHDR     = true;

    uint64_t offset = 0;
    for (uint32_t m = 0; m < NUM_MIPS; ++m) {
        uint32_t mW        = std::max(1u, BASE_W >> m);
        uint32_t mH        = std::max(1u, BASE_H >> m);
        float    roughness = roughnesses[m];
        uint64_t mipSz     = mW * mH * 4 * sizeof(float);

        SA_LOG_INFO("  prefiltered mip {} ({}×{} roughness={:.2f})", m, mW, mH, roughness);

        std::vector<float> pixels(mW * mH * 4);

        for (uint32_t py = 0; py < mH; ++py) {
            float v = (py + 0.5f) / mH;
            for (uint32_t px = 0; px < mW; ++px) {
                float u = (px + 0.5f) / mW;
                glm::vec3 R = EquirectToDir(u, v);  // reflection direction
                glm::vec3 N = R;                     // simplified: V = N = R

                glm::vec3 prefilteredColor(0.0f);
                float     totalWeight = 0.0f;

                for (uint32_t i = 0; i < N_SAMPLES; ++i) {
                    glm::vec2 Xi = Hammersley(i, N_SAMPLES);
                    glm::vec3 Hv = ImportanceSampleGGX(Xi, N, roughness);
                    glm::vec3 L  = glm::normalize(2.0f * glm::dot(R, Hv) * Hv - R);
                    float NdotL  = std::max(glm::dot(N, L), 0.0f);
                    if (NdotL > 0.0f) {
                        prefilteredColor += SampleEquirect(hdr, L) * NdotL;
                        totalWeight      += NdotL;
                    }
                }

                glm::vec3 color = (totalWeight > 0.0f)
                                ? prefilteredColor / totalWeight
                                : glm::vec3(0.0f);
                float* dst = pixels.data() + (py * mW + px) * 4;
                dst[0] = color.r; dst[1] = color.g; dst[2] = color.b; dst[3] = 1.0f;
            }
        }

        tex.mips.push_back({ offset, mipSz });
        tex.data.resize(offset + mipSz);
        std::memcpy(tex.data.data() + offset, pixels.data(), mipSz);
        offset += mipSz;
    }
    return tex;
}

// ─── BRDF LUT ─────────────────────────────────────────────────────────────────
// Smith-GGX split-sum: X = NdotV, Y = roughness → (scale, bias) in RG.

static float GeomSchlickGGX_IBL(float NdotV, float roughness) {
    float k = (roughness * roughness) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k + 1e-7f);
}

static Resource::CookedTexture BakeBrdfLut(const AssetID& id) {
    constexpr uint32_t SIZE      = 512;
    constexpr uint32_t N_SAMPLES = 1024;

    SA_LOG_INFO("  brdf_lut {}×{}", SIZE, SIZE);

    std::vector<float> pixels(SIZE * SIZE * 4, 0.0f);

    for (uint32_t py = 0; py < SIZE; ++py) {
        float roughness = (py + 0.5f) / SIZE;
        for (uint32_t px = 0; px < SIZE; ++px) {
            float NdotV = (px + 0.5f) / SIZE;
            glm::vec3 V = { std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV };
            glm::vec3 N = { 0.0f, 0.0f, 1.0f };

            float A = 0.0f, B = 0.0f;
            for (uint32_t i = 0; i < N_SAMPLES; ++i) {
                glm::vec2 Xi = Hammersley(i, N_SAMPLES);
                glm::vec3 Hv = ImportanceSampleGGX(Xi, N, roughness);
                glm::vec3 L  = glm::normalize(2.0f * glm::dot(V, Hv) * Hv - V);

                float NdotL = std::max(L.z,             0.0f);
                float NdotH = std::max(Hv.z,            0.0f);
                float VdotH = std::max(glm::dot(V, Hv), 0.0f);

                if (NdotL > 0.0f) {
                    float G     = GeomSchlickGGX_IBL(NdotV, roughness)
                                * GeomSchlickGGX_IBL(NdotL, roughness);
                    float G_Vis = G * VdotH / (NdotH * NdotV + 1e-7f);
                    float Fc    = std::pow(1.0f - VdotH, 5.0f);
                    A += (1.0f - Fc) * G_Vis;
                    B += Fc          * G_Vis;
                }
            }

            float* dst = pixels.data() + (py * SIZE + px) * 4;
            dst[0] = A / N_SAMPLES;
            dst[1] = B / N_SAMPLES;
            dst[2] = 0.0f;
            dst[3] = 1.0f;
        }
    }

    Resource::CookedTexture tex;
    tex.id        = id;
    tex.width     = SIZE;
    tex.height    = SIZE;
    tex.mipLevels = 1;
    tex.format    = Resource::CookedTextureFormat::RGBA32F;
    tex.srgb      = false;
    tex.isHDR     = true;
    uint64_t sz   = SIZE * SIZE * 4 * sizeof(float);
    tex.mips.push_back({ 0, sz });
    tex.data.resize(sz);
    std::memcpy(tex.data.data(), pixels.data(), sz);
    return tex;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool Bake(const Resource::ImageData& hdr,
          const IblAssetIDs&         ids,
          const fs::path&            outputDir,
          bool                       force) {
    if (!hdr.isHDR || hdr.pixelsHDR.empty()) {
        SA_LOG_ERROR("IblBake::Bake — input is not a valid HDR image");
        return false;
    }
    fs::create_directories(outputDir);

    bool ok = true;

    // ── Irradiance ────────────────────────────────────────────────────────────
    fs::path irrPath = outputDir / (ids.irradiance.ToString() + ".satex");
    if (force || !fs::exists(irrPath)) {
        SA_LOG_INFO("IblBake: baking irradiance map…");
        auto tex = BakeIrradiance(hdr, ids.irradiance);
        ok &= Resource::SaveCookedTexture(tex, irrPath.string());
        if (ok) SA_LOG_INFO("IblBake: irradiance → {}", irrPath.filename().string());
    } else {
        SA_LOG_INFO("IblBake: irradiance up-to-date, skipping");
    }

    // ── Prefiltered env ───────────────────────────────────────────────────────
    fs::path envPath = outputDir / (ids.prefiltered.ToString() + ".satex");
    if (force || !fs::exists(envPath)) {
        SA_LOG_INFO("IblBake: baking prefiltered env (5 mips)…");
        auto tex = BakePrefiltered(hdr, ids.prefiltered);
        ok &= Resource::SaveCookedTexture(tex, envPath.string());
        if (ok) SA_LOG_INFO("IblBake: prefiltered → {}", envPath.filename().string());
    } else {
        SA_LOG_INFO("IblBake: prefiltered env up-to-date, skipping");
    }

    // ── BRDF LUT ──────────────────────────────────────────────────────────────
    fs::path lutPath = outputDir / (ids.brdfLut.ToString() + ".satex");
    if (force || !fs::exists(lutPath)) {
        SA_LOG_INFO("IblBake: baking BRDF LUT…");
        auto tex = BakeBrdfLut(ids.brdfLut);
        ok &= Resource::SaveCookedTexture(tex, lutPath.string());
        if (ok) SA_LOG_INFO("IblBake: brdf_lut → {}", lutPath.filename().string());
    } else {
        SA_LOG_INFO("IblBake: BRDF LUT up-to-date, skipping");
    }

    return ok;
}

} // namespace StellarAlia::IblBake
