#pragma once
#include <cstdint>
#include <string>

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
#  include <sys/sysctl.h>
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

// Returns the CPU brand string (e.g. "Intel(R) Core(TM) i7-10700K CPU @ 3.80GHz").
// Read from the OS once; cheap to call but not intended for every frame.
inline std::string GetCpuName() noexcept {
#if defined(_WIN32)
    // ProcessorNameString is stored in the registry by the OS at boot.
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &key) == ERROR_SUCCESS) {
        char buf[256] = {};
        DWORD size = sizeof(buf);
        RegQueryValueExA(key, "ProcessorNameString", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(buf), &size);
        RegCloseKey(key);
        std::string s(buf);
        // Trim leading/trailing spaces that some BIOSes leave in
        const auto first = s.find_first_not_of(' ');
        if (first == std::string::npos) return "(unknown CPU)";
        return s.substr(first, s.find_last_not_of(' ') - first + 1);
    }
    return "(unknown CPU)";

#elif defined(__linux__)
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (!f) return "(unknown CPU)";
    char line[256];
    std::string name;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "model name", 10) == 0) {
            const char* colon = strchr(line, ':');
            if (colon) {
                ++colon;
                while (*colon == ' ') ++colon;
                name = colon;
                if (!name.empty() && name.back() == '\n') name.pop_back();
            }
            break;
        }
    }
    fclose(f);
    return name.empty() ? "(unknown CPU)" : name;

#elif defined(__APPLE__)
    char buf[256] = {};
    size_t size = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0)
        return std::string(buf);
    return "(unknown CPU)";

#else
    return "(unknown CPU)";
#endif
}

} // namespace StellarAlia::Platform
