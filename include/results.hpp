#ifndef RESULTS_HPP
#define RESULTS_HPP

#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "config.hpp"

using json = nlohmann::json;

/**
 * @brief Stores benchmark output + metadata and writes JSON.
 *
 * Design goals:
 * - machine-readable JSON output
 * - include run config for reproducibility
 * - support sweeps (many points per run)
 */
struct BenchmarkResult {
    // ---------- Metadata: OS / Compiler ----------
    std::string os =
#ifdef _WIN32
        "Windows";
#elif defined(__linux__)
        "Linux";
#else
        "Unknown OS";
#endif

    std::string compiler =
#ifdef _MSC_VER
        "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
        "Clang " + std::to_string(__clang_major__) + "." +
                  std::to_string(__clang_minor__);
#elif defined(__GNUC__)
        "GCC " + std::to_string(__GNUC__) + "." +
                std::to_string(__GNUC_MINOR__);
#else
        "Unknown Compiler";
#endif

    // ---------- High-level stats (optional aggregate) ----------
    long long total_ns = 0;
    double avg_ns = 0.0;
    double bandwidth_gb_s = 0.0;
    double gflops = 0.0;

    // ---------- Sweep output ----------
    struct Point {
        std::size_t bytes = 0;        // working set size in bytes (ONE array size)
        double median_ns = 0.0;       // median iteration time (ns)
        double p95_ns = 0.0;          // p95 iteration time (ns)
        double min_ns = 0.0;          // minimum iteration time (ns)
        double max_ns = 0.0;          // maximum iteration time (ns)
        double stddev_ns = 0.0;       // standard deviation of iteration times (ns)
        double bandwidth_gb_s = 0.0;  // effective GB/s (based on bytes touched)
        double checksum = 0.0;        // sampled checksum (DCE/correctness signal)
        std::string kernel;           // kernel name for this point
    };

    std::vector<Point> sweep_points;

    // ---------- JSON writer ----------
    void save(const Config& conf) const {
        json j;

        // Timestamp
        const auto now_tp = std::chrono::system_clock::now();
        const std::time_t now = std::chrono::system_clock::to_time_t(now_tp);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");

        // Metadata
        j["metadata"]["timestamp"] = ss.str();
        j["metadata"]["platform"]["os"] = os;
        j["metadata"]["platform"]["compiler"] = compiler;

#ifdef _MSC_VER
        j["metadata"]["platform"]["cpp_standard"] = _MSVC_LANG;
#else
        j["metadata"]["platform"]["cpp_standard"] = __cplusplus;
#endif

        // Config (CLI)
        j["config"]["kernel"]  = conf.kernel;
        j["config"]["size"]    = conf.size;
        j["config"]["threads"] = conf.threads;
        j["config"]["iters"]   = conf.iters;
        j["config"]["warmup"]  = conf.warmup;
        j["config"]["seed"]    = conf.seed;
        j["config"]["out"]     = conf.out;

        // Aggregate stats (if you use them)
        j["stats"]["performance"]["total_time_ns"]  = total_ns;
        j["stats"]["performance"]["avg_ns_per_op"]  = avg_ns;
        j["stats"]["performance"]["bandwidth_gb_s"] = bandwidth_gb_s;
        j["stats"]["performance"]["gflops"]         = gflops;

        // Sweep points
        if (!sweep_points.empty()) {
            for (const auto& pt : sweep_points) {
                j["stats"]["sweep"].push_back({
                    {"kernel", pt.kernel},
                    {"bytes", pt.bytes},
                    {"median_ns", pt.median_ns},
                    {"p95_ns", pt.p95_ns},
                    {"min_ns", pt.min_ns},
                    {"max_ns", pt.max_ns},
                    {"stddev_ns", pt.stddev_ns},
                    {"bandwidth_gb_s", pt.bandwidth_gb_s},
                    {"checksum", pt.checksum}
                });
            }
        }

        // Write file
        std::ofstream file(conf.out);
        if (!file) {
            std::cerr << "Error: failed to open output file: " << conf.out << "\n";
            return;
        }
        file << j.dump(4);
        file.close();

        std::cout << "[Results] JSON written to: " << conf.out << "\n";
    }
};

#endif // RESULTS_HPP
