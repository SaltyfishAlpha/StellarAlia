#pragma once
#include <cstdint>

// Platform-specific includes
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <psapi.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "Psapi.lib")
#  endif
#elif defined(__linux__)
#  include <cstdio>
#  include <cstring>
#elif defined(__APPLE__)
#  include <sys/resource.h>
#endif

namespace StellarAlia::Platform {

// Returns the current process physical memory usage in bytes.
// Windows : Working Set (pages currently in physical RAM).
// Linux   : VmRSS from /proc/self/status (current RSS).
// macOS   : ru_maxrss from getrusage (bytes, current RSS on macOS ≥ 10.8).
// Other   : returns 0 (stub — add platform impl as needed).
inline uint64_t GetProcessMemoryBytes() noexcept {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<uint64_t>(pmc.WorkingSetSize);
    return 0;

#elif defined(__linux__)
    // /proc/self/status reports VmRSS in kB — current resident set size.
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[128];
    uint64_t rss = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, " %llu", &rss);
            rss *= 1024u;
            break;
        }
    }
    fclose(f);
    return rss;

#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<uint64_t>(usage.ru_maxrss); // bytes on macOS
    return 0;

#else
    return 0; // unsupported platform
#endif
}

} // namespace StellarAlia::Platform
