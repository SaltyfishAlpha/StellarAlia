#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Thin profiling wrapper around Tracy.
//
// CPU zones:
//   SA_PROFILE_SCOPE()           — zone named by __FUNCTION__
//   SA_PROFILE_SCOPE_N(name)     — zone with explicit string literal name
//   SA_PROFILE_SCOPE_C(name,col) — zone with name + 0xRRGGBB colour
//   SA_PROFILE_FRAME()           — frame boundary (call once per frame)
//   SA_PROFILE_PLOT(name, val)   — numeric time-series plot
//   SA_PROFILE_MESSAGE(str, len) — log message on the timeline
//
// Runtime toggle (Debug builds only):
//   Profiler::SetEnabled(bool)   — pause / resume zone collection at runtime
//   Profiler::IsEnabled()        — query current state
//
// CMake TRACY_ENABLE (Debug/RelWithDebInfo) compiles Tracy in.
// Release builds: TRACY_ENABLE absent → zero binary overhead (no atomics, no
// ScopedZone objects, macros expand to nothing).
// ─────────────────────────────────────────────────────────────────────────────

#include <tracy/Tracy.hpp>

// ── Private token-paste helpers (independent of Tracy internals) ──────────────
#define SA_PP_CAT2_(a, b) a##b
#define SA_PP_CAT_(a, b)  SA_PP_CAT2_(a, b)

#ifdef TRACY_ENABLE
#include <atomic>

namespace StellarAlia {
struct Profiler {
    static bool IsEnabled() noexcept {
        return s_enabled.load(std::memory_order_relaxed);
    }
    static void SetEnabled(bool v) noexcept {
        s_enabled.store(v, std::memory_order_relaxed);
    }
    static inline std::atomic<bool> s_enabled { true };
};
} // namespace StellarAlia

// FrameMark is a plain statement — wrap in a guarded do/while.
#define SA_PROFILE_FRAME() \
    do { if (::StellarAlia::Profiler::IsEnabled()) { FrameMark; } } while (0)

// Use Tracy's public ScopedZone(srcloc, depth, active) constructor.
// We build our own unique variable names via __LINE__ (one call-site per line).
// static constexpr loc: lives forever at its call-site, no capture needed.
// ScopedZone zone:       RAII, lives for the enclosing scope.
#define SA_PROFILE_SCOPE() \
    static constexpr tracy::SourceLocationData \
    SA_PP_CAT_(___sa_loc_, __LINE__) { nullptr, __FUNCTION__, __FILE__, (uint32_t)__LINE__, 0 }; \
    tracy::ScopedZone SA_PP_CAT_(___sa_zone_, __LINE__) \
        { &SA_PP_CAT_(___sa_loc_, __LINE__), 0, ::StellarAlia::Profiler::IsEnabled() }

#define SA_PROFILE_SCOPE_N(name) \
    static constexpr tracy::SourceLocationData \
    SA_PP_CAT_(___sa_loc_, __LINE__) { nullptr, name, __FILE__, (uint32_t)__LINE__, 0 }; \
    tracy::ScopedZone SA_PP_CAT_(___sa_zone_, __LINE__) \
        { &SA_PP_CAT_(___sa_loc_, __LINE__), 0, ::StellarAlia::Profiler::IsEnabled() }

#define SA_PROFILE_SCOPE_C(name, col) \
    static constexpr tracy::SourceLocationData \
    SA_PP_CAT_(___sa_loc_, __LINE__) { nullptr, name, __FILE__, (uint32_t)__LINE__, col }; \
    tracy::ScopedZone SA_PP_CAT_(___sa_zone_, __LINE__) \
        { &SA_PP_CAT_(___sa_loc_, __LINE__), 0, ::StellarAlia::Profiler::IsEnabled() }

#define SA_PROFILE_PLOT(name, val) \
    do { if (::StellarAlia::Profiler::IsEnabled()) \
        TracyPlot(name, static_cast<double>(val)); } while (0)

#define SA_PROFILE_MESSAGE(str, len) \
    do { if (::StellarAlia::Profiler::IsEnabled()) \
        TracyMessage(str, len); } while (0)

#else
// ── Release / TRACY_ENABLE absent — absolute zero overhead ───────────────────
namespace StellarAlia {
struct Profiler {
    static bool IsEnabled() noexcept { return false; }
    static void SetEnabled(bool) noexcept {}
};
} // namespace StellarAlia

#define SA_PROFILE_FRAME()
#define SA_PROFILE_SCOPE()
#define SA_PROFILE_SCOPE_N(name)
#define SA_PROFILE_SCOPE_C(name, col)
#define SA_PROFILE_PLOT(name, val)    (void)(val)
#define SA_PROFILE_MESSAGE(str, len)
#endif
