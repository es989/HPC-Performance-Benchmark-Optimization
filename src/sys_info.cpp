#include "sys_info.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

// ---------- Helpers ----------
static std::string trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

// ---------- CPU model ----------
static std::string get_cpu_model() {
#if defined(__linux__)
    // Parse /proc/cpuinfo (no shell, no external deps).
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                return trim(line.substr(colon + 1));
            }
        }
    }
    return "Unknown Linux CPU";
#elif defined(_WIN32)
    return "Unknown CPU (Windows)";
#elif defined(__APPLE__)
    return "Unknown CPU (macOS)";
#else
    return "Unknown CPU";
#endif
}

// ---------- OS distro ----------
static std::string get_os_distro() {
#if defined(__linux__)
    // Nice label for README (Ubuntu 22.04, etc).
    std::ifstream osrelease("/etc/os-release");
    std::string line;
    while (std::getline(osrelease, line)) {
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            std::string name = line.substr(std::string("PRETTY_NAME=").size());
            name.erase(std::remove(name.begin(), name.end(), '"'), name.end());
            return trim(name);
        }
    }
    return "Unknown Linux Distro";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown OS";
#endif
}

// ---------- OS kernel ----------
static std::string get_os_kernel() {
#if defined(__linux__) || defined(__APPLE__)
    // POSIX uname (stable, no external commands).
    struct utsname u {};
    if (uname(&u) == 0) {
        return std::string(u.sysname) + " " + std::string(u.release);
    }
    return "Unknown POSIX Kernel";
#elif defined(_WIN32)
    return "Windows NT";
#else
    return "Unknown Kernel";
#endif
}

// ---------- RAM total (rounded GiB) ----------
static uint64_t get_ram_total_gib_rounded() {
#if defined(__linux__) || defined(__APPLE__)
    // sysconf avoids text parsing + avoids external commands.
    // IMPORTANT: cast BEFORE multiply (prevents overflow on 32-bit long).
    const long pages_l     = sysconf(_SC_PHYS_PAGES);
    const long page_size_l = sysconf(_SC_PAGE_SIZE);

    if (pages_l <= 0 || page_size_l <= 0) {
        return 0; // Unknown
    }

    const uint64_t pages     = static_cast<uint64_t>(pages_l);
    const uint64_t page_size = static_cast<uint64_t>(page_size_l);

    const uint64_t total_bytes = pages * page_size;

    const uint64_t one_gib  = 1024ULL * 1024ULL * 1024ULL;
    const uint64_t half_gib = one_gib / 2;

    // Round-to-nearest GiB (no float, no <cmath>).
    return (total_bytes + half_gib) / one_gib;
#else
    return 0;
#endif
}

static std::string format_gib_pretty(uint64_t gib) {
    return (gib == 0) ? "Unknown RAM" : (std::to_string(gib) + " GiB");
}

// ---------- Cache Sizes ----------
static void collect_cache_sizes(benchmark::SystemInfo& info) {
#if defined(_WIN32)
    DWORD buffer_size = 0;
    GetLogicalProcessorInformation(nullptr, &buffer_size);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && buffer_size > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(buffer.data(), &buffer_size)) {
            for (const auto& info_struct : buffer) {
                if (info_struct.Relationship == RelationCache) {
                    const auto& cache = info_struct.Cache;
                    if (cache.Level == 1 && (cache.Type == CacheData || cache.Type == CacheUnified)) {
                        if (info.cache_l1_bytes == 0) info.cache_l1_bytes = cache.Size;
                    } else if (cache.Level == 2) {
                        if (info.cache_l2_bytes == 0) info.cache_l2_bytes = cache.Size;
                    } else if (cache.Level == 3) {
                        // LLC is usually shared, so we sum or take max depending on architecture.
                        // For simplicity, we'll just take the first L3 size we see, which is often the per-CCX or total size.
                        if (info.cache_llc_bytes == 0) info.cache_llc_bytes = cache.Size;
                    }
                }
            }
        }
    }
#elif defined(__linux__)
    auto read_sysfs_int = [](const std::string& path) -> uint64_t {
        std::ifstream f(path);
        uint64_t val = 0;
        if (f >> val) return val;
        return 0;
    };
    
    // L1 Data
    info.cache_l1_bytes = read_sysfs_int("/sys/devices/system/cpu/cpu0/cache/index0/size");
    if (info.cache_l1_bytes == 0) {
        // Sometimes size has 'K' suffix
        std::ifstream f("/sys/devices/system/cpu/cpu0/cache/index0/size");
        std::string s;
        if (f >> s && !s.empty()) {
            if (s.back() == 'K') info.cache_l1_bytes = std::stoull(s.substr(0, s.size()-1)) * 1024;
        }
    }

    // L2
    info.cache_l2_bytes = read_sysfs_int("/sys/devices/system/cpu/cpu0/cache/index2/size");
    if (info.cache_l2_bytes == 0) {
        std::ifstream f("/sys/devices/system/cpu/cpu0/cache/index2/size");
        std::string s;
        if (f >> s && !s.empty()) {
            if (s.back() == 'K') info.cache_l2_bytes = std::stoull(s.substr(0, s.size()-1)) * 1024;
        }
    }

    // L3 (LLC)
    info.cache_llc_bytes = read_sysfs_int("/sys/devices/system/cpu/cpu0/cache/index3/size");
    if (info.cache_llc_bytes == 0) {
        std::ifstream f("/sys/devices/system/cpu/cpu0/cache/index3/size");
        std::string s;
        if (f >> s && !s.empty()) {
            if (s.back() == 'K') info.cache_llc_bytes = std::stoull(s.substr(0, s.size()-1)) * 1024;
        }
    }
#endif
}

// ---------- Public API ----------
benchmark::SystemInfo benchmark::collect_system_info() {
    benchmark::SystemInfo info;

    // ---------- Compiler (compile-time) ----------
    info.compiler_info = benchmark::get_compiler_info();

    // ---------- CPU / cores (runtime) ----------
    info.cpu_model = get_cpu_model();

    // hardware_concurrency() may return 0 -> fallback to 1.
    const unsigned int cores = std::thread::hardware_concurrency();
    info.logical_cores = (cores > 0) ? cores : 1;

    // ---------- RAM (runtime) ----------
    info.ram_total_gib = get_ram_total_gib_rounded();
    info.ram_total_pretty = format_gib_pretty(info.ram_total_gib);

    // ---------- OS (runtime) ----------
    info.os_distro = get_os_distro();
    info.os_kernel = get_os_kernel();

    // ---------- Caches (runtime) ----------
    collect_cache_sizes(info);

    return info;
}