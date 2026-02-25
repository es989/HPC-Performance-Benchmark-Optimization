#include "config.hpp"
#include "results.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "aligned_buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <random>
#include <vector>

namespace {

/**
 * @brief A node in the linked list used for pointer chasing.
 *
 * The `alignas(64)` attribute ensures that each node occupies exactly one
 * cache line (64 bytes on most modern architectures). This prevents
 * "false sharing" and ensures that when we fetch a node, we are fetching
 * exactly one cache line, making the latency measurement accurate.
 *
 * The `pad` array fills the rest of the 64 bytes after the 4-byte `next` index.
 */
struct alignas(64) Node {
    std::uint32_t next;
    std::uint32_t pad[15]; // 64B total (1 + 15)*4
};

/**
 * @brief Build a sweep of working set sizes for the latency benchmark.
 *
 * Generates a vector of sizes (in bytes) to test. The sizes start very small
 * (4KB) to fit entirely within the L1 cache, and grow up to 256MB to force
 * accesses to Main Memory (DRAM). This allows us to plot the latency at each
 * level of the memory hierarchy.
 *
 * @return A vector of sizes in bytes.
 */
static std::vector<std::size_t> build_sweep_bytes() {
    std::vector<std::size_t> sizes;

    // Start smaller for latency (include 4KB/8KB) up to 512MB.
    for (std::size_t kb = 4; kb <= 256; kb *= 2) sizes.push_back(kb * 1024);
    for (std::size_t kb = 512; kb <= 8192; kb *= 2) sizes.push_back(kb * 1024);
    // Cap at 256MB by default to avoid allocation failures on smaller-memory machines.
    for (std::size_t mb = 16; mb <= 256; mb *= 2) sizes.push_back(mb * 1024ull * 1024ull);

    return sizes;
}

/**
 * @brief Convert a size in bytes to the number of `Node` elements.
 *
 * @param bytes The total size in bytes.
 * @return The number of `Node` elements (bytes / 64).
 */
static std::size_t bytes_to_nodes(std::size_t bytes) {
    return bytes / sizeof(Node);
}

/**
 * @brief Build a randomized single-cycle linked list.
 *
 * This is the core setup for a pointer-chasing benchmark. It creates a linked
 * list where every node is visited exactly once before returning to the start.
 * The order of nodes is randomized to defeat hardware prefetchers. If the
 * accesses were sequential, the CPU would prefetch the next cache line, hiding
 * the true memory latency.
 *
 * @param nodes Pointer to the array of nodes.
 * @param n Total number of nodes.
 * @param seed Random seed for reproducibility.
 */
static void build_random_cycle(Node* nodes, std::size_t n, std::uint32_t seed) {
    // Build a single random cycle permutation: next[i] = perm[i+1], last points to first.
    std::vector<std::uint32_t> idx(n);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i) idx[i] = i;

    // Shuffle the indices to create a random access pattern.
    std::mt19937 rng(seed);
    std::shuffle(idx.begin(), idx.end(), rng);

    // Link each node to the next random node in the shuffled list.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        nodes[idx[i]].next = idx[i + 1];
    }
    // Close the cycle by linking the last node back to the first.
    nodes[idx[n - 1]].next = idx[0];

    // Touch pads so the compiler can't assume they're unused and optimize them away.
    nodes[idx[0]].pad[0] = 1;
}

} // namespace

/**
 * @brief Pointer-chasing latency benchmark runner.
 *
 * This function executes the latency benchmark across a range of working set
 * sizes. For each size, it allocates an array of `Node`s, builds a randomized
 * linked list, and then times how long it takes to traverse the list.
 *
 * The result is a sweep of `ns_per_access` vs working-set size (bytes), which
 * clearly shows the latency of L1, L2, LLC, and DRAM.
 *
 * @param conf The parsed configuration (warmup, iters, prefault, aligned, etc.).
 * @param res The result object to populate with sweep points.
 */
void run_latency_bench(const Config& conf, BenchmarkResult& res) {
    const auto sweep = build_sweep_bytes();
    const bool use_aligned = conf.aligned;
    const std::size_t alignment = 64;

    for (std::size_t size_bytes : sweep) {
        std::size_t n = bytes_to_nodes(size_bytes);
        if (n < 2) continue;

        // Allocate nodes (aligned, one node per cache line).
        std::vector<Node> nodes_vec;
        benchmark::AlignedBuffer<Node> nodes_aligned;
        Node* nodes = nullptr;

        try {
            if (use_aligned) {
                nodes_aligned = benchmark::AlignedBuffer<Node>(n, alignment);
                nodes = nodes_aligned.data();
                // Value-init (posix_memalign/_aligned_malloc are uninitialized)
                for (std::size_t i = 0; i < n; ++i) nodes[i] = Node{0, {0}};
            } else {
                nodes_vec.assign(n, Node{0, {0}});
                nodes = nodes_vec.data();
            }
        } catch (const std::bad_alloc&) {
            std::cerr << "[Latency] Allocation failed at bytes=" << size_bytes
                      << " (nodes=" << n << "). Stopping sweep.\n";
            break;
        }

        // Optional prefault.
        if (conf.prefault) {
            const std::size_t page_nodes = 4096 / sizeof(Node);
            const std::size_t step = std::max<std::size_t>(1, page_nodes);
            for (std::size_t i = 0; i < n; i += step) {
                volatile std::uint32_t t = nodes[i].next;
                nodes[i].next = t;
            }
            do_not_optimize_away(nodes[0].next);
        }

        build_random_cycle(nodes, n, static_cast<std::uint32_t>(conf.seed) ^ static_cast<std::uint32_t>(size_bytes));

        // Choose number of dependent loads per iteration.
        // For very large working sets, scaling as O(n) can get too slow.
        // Clamp steps to keep automation runs feasible while still being a
        // true dependent-load (pointer chase) measurement.
        const std::size_t min_steps = 200'000;
        const std::size_t max_steps = 5'000'000;
        const std::size_t steps = std::min<std::size_t>(std::max<std::size_t>(n, min_steps), max_steps);

        auto chase = [&](std::uint32_t start) -> std::uint32_t {
            std::uint32_t cur = start;
            for (std::size_t i = 0; i < steps; ++i) {
                cur = nodes[cur].next;
            }
            return cur;
        };

        // Warmup
        std::uint32_t sink = 0;
        for (int w = 0; w < conf.warmup; ++w) {
            sink = chase(static_cast<std::uint32_t>(w % n));
            do_not_optimize_away(sink);
        }

        std::vector<long long> samples;
        samples.reserve(conf.iters);

        for (int it = 0; it < conf.iters; ++it) {
            Timer t;
            clobber_memory();
            t.start();

            sink = chase(static_cast<std::uint32_t>(it % n));

            clobber_memory();
            const long long ns = t.elapsed_ns();
            samples.push_back(ns);
            do_not_optimize_away(sink);
        }

        if (samples.empty()) continue;
        std::sort(samples.begin(), samples.end());
        const long long min_sample = samples.front();
        const long long max_sample = samples.back();

        const double med = percentile_ns(samples, 50.0);
        const double p95 = percentile_ns(samples, 95.0);

        std::vector<double> samples_double;
        samples_double.reserve(samples.size());
        for (auto ns : samples) {
            samples_double.push_back(static_cast<double>(ns));
        }
        const double stddev = compute_stddev(samples_double);

        const double ns_per_access = (steps > 0) ? (med / static_cast<double>(steps)) : 0.0;

        BenchmarkResult::Point pt;
        pt.kernel = "ptr_chase";
        pt.bytes = size_bytes;
        pt.median_ns = med;
        pt.p95_ns = p95;
        pt.min_ns = static_cast<double>(min_sample);
        pt.max_ns = static_cast<double>(max_sample);
        pt.stddev_ns = stddev;
        pt.bandwidth_gb_s = 0.0;
        pt.ns_per_access = ns_per_access;
        pt.checksum = static_cast<double>(sink);

        res.sweep_points.push_back(pt);

        std::cout << "[Latency] bytes=" << size_bytes << " median_ns=" << med
                  << " ns_per_access=" << ns_per_access << "\n";
    }
}
