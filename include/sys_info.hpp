#pragma once

#include <cstdint>
#include <string>

namespace benchmark {

/**
 * @brief Auto-collect minimal platform specs for reproducible benchmarks.
 *
 * Design goals:
 * - minimal + stable (CPU/RAM/OS/Compiler)
 * - no external commands (no lscpu/uname/gcc --version)
 * - store both numeric + pretty where useful
 */
struct SystemInfo {
    // ---------- CPU ----------
    std::string cpu_model;             // human-readable CPU model
    uint32_t    logical_cores = 0;     // hw threads (safe fallback)

    // ---------- RAM ----------
    uint64_t    ram_total_gib = 0;     // numeric (rounded GiB) -> scripts/plots
    std::string ram_total_pretty;      // "16 GiB" -> README/UI

    // ---------- Caches ----------
    uint64_t    cache_l1_bytes = 0;    // L1 Data cache size per core
    uint64_t    cache_l2_bytes = 0;    // L2 cache size per core
    uint64_t    cache_llc_bytes = 0;   // Last Level Cache (L3) total size

    // ---------- OS ----------
    std::string os_distro;             // Linux: PRETTY_NAME from /etc/os-release
    std::string os_kernel;             // POSIX: uname (sysname + release)

    // ---------- Compiler ----------
    std::string compiler_info;         // compile-time compiler string
};

// Get all system specs (runtime + compile-time)
SystemInfo collect_system_info();

// Helper: compiler info at compile-time (zero runtime cost)
inline std::string get_compiler_info() {
#if defined(__clang__)
    return "Clang " + std::string(__clang_version__);
#elif defined(__GNUC__)
    return "GCC " + std::string(__VERSION__);
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "Unknown Compiler";
#endif
}

} 