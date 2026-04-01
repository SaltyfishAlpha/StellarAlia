#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <random>
#include <sstream>
#include <iomanip>

namespace StellarAlia {

// 128-bit UUID v4 (random), stored as two uint64_t.
// Serialized as "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
struct AssetID {
    uint64_t hi = 0;
    uint64_t lo = 0;

    bool IsValid() const { return hi != 0 || lo != 0; }

    static AssetID Invalid() { return {0, 0}; }

    static AssetID Generate() {
        // Thread-local RNG — safe, cheap, no mutex.
        thread_local std::mt19937_64 rng(std::random_device{}());
        AssetID id{ rng(), rng() };
        // Set UUID v4 version bits (bits 76-79 = 0100) and variant (bits 64-65 = 10).
        id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
        id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
        return id;
    }

    // "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
    std::string ToString() const {
        uint32_t p1 = static_cast<uint32_t>(hi >> 32);
        uint16_t p2 = static_cast<uint16_t>(hi >> 16);
        uint16_t p3 = static_cast<uint16_t>(hi);
        uint16_t p4 = static_cast<uint16_t>(lo >> 48);
        uint64_t p5 = lo & 0x0000FFFFFFFFFFFFull;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0')
           << std::setw(8)  << p1 << '-'
           << std::setw(4)  << p2 << '-'
           << std::setw(4)  << p3 << '-'
           << std::setw(4)  << p4 << '-'
           << std::setw(12) << p5;
        return ss.str();
    }

    static AssetID FromString(std::string_view s) {
        if (s.size() != 36) return Invalid();
        // Strip dashes: positions 8, 13, 18, 23.
        std::string hex;
        hex.reserve(32);
        for (char c : s) {
            if (c != '-') hex += c;
        }
        if (hex.size() != 32) return Invalid();
        try {
            AssetID id;
            id.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
            id.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
            return id;
        } catch (...) {
            return Invalid();
        }
    }

    bool operator==(const AssetID&) const = default;
    bool operator!=(const AssetID&) const = default;
};

} // namespace StellarAlia
