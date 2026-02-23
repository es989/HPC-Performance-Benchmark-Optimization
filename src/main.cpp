#include "config.hpp"
#include "results.hpp"
#include <iostream>
#include <string>

// חובה לכלול את זה כדי להכיר את StreamOp
#include "stream_kernels.hpp"

// הצהרה על הפונקציה שנמצאת ב-stream_sweep.cpp
void run_stream_sweep(const Config& conf, BenchmarkResult& res, StreamOp op);

// Compute microbenchmark runner (src/compute_bench.cpp)
void run_compute_bench(const Config& conf, BenchmarkResult& res, const std::string& kind);

// Latency pointer-chasing runner (src/latency_bench.cpp)
void run_latency_bench(const Config& conf, BenchmarkResult& res);

int main(int argc, char** argv) {
    Config conf = parse_args(argc, argv);
    BenchmarkResult res;

    std::cout << "--- Starting Benchmark: " << conf.kernel << " ---\n";

    // תמיכה גם בשמות קצרים וגם בארוכים
    if (conf.kernel == "stream") {
        // Default STREAM representative kernel
        run_stream_sweep(conf, res, StreamOp::Triad);
    }
    else if (conf.kernel == "copy" || conf.kernel == "stream_copy") {
        run_stream_sweep(conf, res, StreamOp::Copy);
    } 
    else if (conf.kernel == "scale" || conf.kernel == "stream_scale") {
        run_stream_sweep(conf, res, StreamOp::Scale);
    } 
    else if (conf.kernel == "add" || conf.kernel == "stream_add") {
        run_stream_sweep(conf, res, StreamOp::Add);
    } 
    else if (conf.kernel == "triad" || conf.kernel == "stream_triad") {
        run_stream_sweep(conf, res, StreamOp::Triad);
    }
    else if (conf.kernel == "flops" || conf.kernel == "fma" || conf.kernel == "dot" || conf.kernel == "saxpy") {
        run_compute_bench(conf, res, conf.kernel);
    } 
    else if (conf.kernel == "latency") {
        run_latency_bench(conf, res);
    }
    else {
        std::cerr << "Error: Unknown kernel: " << conf.kernel << "\n";
        return 1;
    }

    res.save(conf);
    std::cout << "Done.\n";
    return 0;
}