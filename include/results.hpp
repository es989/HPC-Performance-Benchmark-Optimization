// if file is included twice it wont throw redefinition
#ifndef RESULTS_HPP
#define RESULTS_HPP

#include <nlohmann/json.hpp>

#include <chrono>     
#include <ctime>      // std::time_t, std::localtime
#include <fstream>    
#include <iomanip>    
#include <iostream>   
#include <sstream>    
#include <string>     

#include "config.hpp"

using json = nlohmann::json;

struct BenchmarkResult {
    // -------- 1) Metadata: Platform & Compiler --------

    // Detect OS at compile time
    std::string os =
#ifdef _WIN32
        "Windows";
#elif defined(__linux__)
        "Linux";
#else
        "Unknown OS";
#endif

    // Detect compiler at compile time
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

    // -------- 2) Performance Statistics --------
    // (values are filled by the benchmark code)

    long long total_ns = 0;      // total time across all iterations
    double avg_ns = 0.0;         // average nanoseconds per operation
    double bandwidth_gb_s = 0.0; // memory kernels (copy/scale/add/triad)
    double gflops = 0.0;         // compute kernels (flops/fma)

    // -------- 3) Save Results to JSON --------

    void save(const Config& conf) const {
        json j;

        // ---- Metadata section ----

        // Create a readable timestamp
        const auto now_tp = std::chrono::system_clock::now();
        const std::time_t now = std::chrono::system_clock::to_time_t(now_tp);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S");

        j["metadata"]["timestamp"] = ss.str();
        j["metadata"]["platform"]["os"] = os;
        j["metadata"]["platform"]["compiler"] = compiler;

        // Store C++ standard used for compilation
#ifdef _MSC_VER
        j["metadata"]["platform"]["cpp_standard"] = _MSVC_LANG;
#else
        j["metadata"]["platform"]["cpp_standard"] = __cplusplus;
#endif

        // ---- Config section (captured from CLI) ----
        j["config"]["kernel"]  = conf.kernel;
        j["config"]["size"]    = conf.size;
        j["config"]["threads"] = conf.threads;
        j["config"]["iters"]   = conf.iters;
        j["config"]["warmup"]  = conf.warmup;
        j["config"]["seed"]    = conf.seed;
        j["config"]["out"]     = conf.out;

        // ---- Performance statistics ----
        j["stats"]["performance"]["total_time_ns"]  = total_ns;
        j["stats"]["performance"]["avg_ns_per_op"]  = avg_ns;
        j["stats"]["performance"]["bandwidth_gb_s"] = bandwidth_gb_s;
        j["stats"]["performance"]["gflops"]         = gflops;

        // ---- Write JSON to file ----
        std::ofstream file(conf.out);
        if (!file) {
            std::cerr << "Error: failed to open output file: "
                      << conf.out << "\n";
            return;
        }

        file << j.dump(4); // pretty-print with 4 spaces
        file.close();

        std::cout << "[Step 6] Results written to: "
                  << conf.out << "\n";
    }
};

#endif // RESULTS_HPP
