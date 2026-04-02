#pragma once

#include <cstdint>
#include <random>

namespace StellarAlia::Core {

// Seedable pseudo-random number generator (Mersenne Twister 64-bit).
// Not thread-safe — use one instance per thread.
//
// Usage:
//   Core::Random rng;                 // seeded from hardware entropy
//   Core::Random rng(42);             // deterministic seed
//   float  f = rng.Float(-1.f, 1.f);
//   int    i = rng.Int(0, 10);
//   bool   b = rng.Bool();
class Random {
public:
    // Seeded from std::random_device (non-deterministic by default).
    Random() : m_engine(std::random_device{}()) {}

    // Explicit seed for reproducible sequences.
    explicit Random(uint64_t seed) : m_engine(seed) {}

    // Uniform float in [min, max].
    float Float(float min = 0.f, float max = 1.f) {
        return std::uniform_real_distribution<float>(min, max)(m_engine);
    }

    // Uniform integer in [min, max] (both inclusive).
    int Int(int min, int max) {
        return std::uniform_int_distribution<int>(min, max)(m_engine);
    }

    // Uniform unsigned integer in [min, max] (both inclusive).
    uint32_t UInt(uint32_t min, uint32_t max) {
        return std::uniform_int_distribution<uint32_t>(min, max)(m_engine);
    }

    // 50/50 boolean.
    bool Bool() {
        return std::bernoulli_distribution(0.5)(m_engine);
    }

    // Re-seed the generator.
    void Seed(uint64_t seed) { m_engine.seed(seed); }

private:
    std::mt19937_64 m_engine;
};

} // namespace StellarAlia::Core
