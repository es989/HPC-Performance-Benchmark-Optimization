#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>     
#include <iostream>   
#include <vector>     
#include <cstddef>    
#include <stdexcept>  // std::exception (catch errors from std::stoi string to int)
#include <cstdlib>    // std::exit    (terminate program with a status code)

// ---- Config struct: holds all benchmark settings in ONE place ----
// Idea: instead of many global variables, keep everything in a single struct.
// This makes it easy to pass configuration around and print it for debugging.
struct Config {
    // These are the default values used if the user does NOT provide CLI flags.

    std::string kernel = "stream";        // which benchmark kernel to run (e.g., "stream", "compute", ...)
    std::string size   = "64MB";          // dataset/problem size as text (we may parse it later to bytes)
    int threads        = 1;               // number of worker threads (must be >= 1)
    int iters          = 100;             // how many measured iterations to run (must be >= 1)
    int warmup         = 10;              // how many warmup iterations (not measured, can be 0)
    std::string out    = "results.json";  // output file name/path
    int seed           = 14;              // RNG seed (useful when workload uses randomness) 14 because it is the day i was born


    
    void print() const {
        std::cout << "--- Benchmark Configuration ---\n";
        std::cout << "Kernel  : " << kernel  << "\n";
        std::cout << "Size    : " << size    << "\n";
        std::cout << "Threads : " << threads << "\n";
        std::cout << "Iters   : " << iters   << "\n";
        std::cout << "Warmup  : " << warmup  << "\n";
        std::cout << "Output  : " << out     << "\n";
        std::cout << "Seed    : " << seed    << "\n";
        std::cout << "-------------------------------\n";
    }
};

// ---- print_help: prints CLI usage ----
// We show the user:
// 1) how to run the program
// 2) which flags exist
// 3) what the default values are
inline void print_help(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n\n"
        << "Options:\n"
        << " --kernel  <name>   (default: stream | allowed: copy, scale, add, triad, flops, fma)\n"
        << "  --size    <str>    (default: 64MB)\n"
        << "  --threads <int>    (default: 1)\n"
        << "  --iters   <int>    (default: 100)\n"
        << "  --warmup  <int>    (default: 10)\n"
        << "  --out     <file>   (default: results.json)\n"
        << "  --seed    <int>    (default: 14)\n"
        << "  --help             show this message\n";
}

// Input:
//   argc = number of arguments
//   argv = array of C-strings (program name + flags + values)
// Output:
//   Config object with values taken from CLI 
inline Config parse_args(int argc, char** argv) {
    // Start with default config
    Config conf;

    // Convert argv into a vector<string> so it's easier to compare values like "--threads"
    // We skip argv[0] because it's the program name ("./bench")
    std::vector<std::string> args(argv + 1, argv + argc);

    //verifies that a flag has a value after it
    // Example: "--threads 4" -> ok
    // Example: "--threads"   -> error (missing value)
    auto need_value = [&](std::size_t i) {
        if (i + 1 >= args.size()) {
            std::cerr << "Error: missing value after '" << args[i] << "'\n";
            print_help(argv[0]);   // show how to use the program
            std::exit(1);          // exit with error code 1 (non-zero means failure)
        }
    };
    //The value is valid for the program
    // try/catch because std::stoi can throw exceptions)
    try {
        for (std::size_t i = 0; i < args.size(); i++) {

            // ---- Help ----
            // If user requested help, print it and exit successfully (0)
            if (args[i] == "--help") {
                print_help(argv[0]);
                std::exit(0);
            }

            // ---- String flags ----
            // These flags expect a string value right after them
            else if (args[i] == "--kernel") {
                need_value(i);          // make sure there is a next token
                conf.kernel = args[++i]; 
            }
            else if (args[i] == "--size") {
                need_value(i);
                conf.size = args[++i];
            }
            else if (args[i] == "--out") {
                need_value(i);
                conf.out = args[++i];
            }

            // ---- Integer flags ----
            // std::stoi converts string -> int
            // It can throw if the input is not a valid integer
            else if (args[i] == "--threads") {
                need_value(i);
                conf.threads = std::stoi(args[++i]);
            }
            else if (args[i] == "--iters") {
                need_value(i);
                conf.iters = std::stoi(args[++i]);
            }
            else if (args[i] == "--warmup") {
                need_value(i);
                conf.warmup = std::stoi(args[++i]);
            }
            else if (args[i] == "--seed") {
                need_value(i);
                conf.seed = std::stoi(args[++i]);
            }

            // ---- Unknown flag ----
            // Very important: we FAIL FAST on unknown flags.
            // This prevents silent mistakes like "--threds 4" (typo) which would otherwise be ignored.
            else {
                std::cerr << "Error: unknown option '" << args[i] << "'\n";
                print_help(argv[0]);
                std::exit(1);
            }
        }
    }
    catch (const std::exception& e) {
        // If stoi failed or other parsing error happened, show a friendly message
        std::cerr << "Error: invalid argument value (" << e.what() << ")\n";
        print_help(argv[0]);
        std::exit(1);
    }

    // ---- Validation step (sanity checks) ----
    // This ensures the benchmark won't run with nonsense settings.
    // Example: threads=0 doesn't make sense, iters=0 means "do nothing".
    if (conf.threads < 1) {
        std::cerr << "Error: --threads must be >= 1\n";
        std::exit(1);
    }
    if (conf.iters < 1) {
        std::cerr << "Error: --iters must be >= 1\n";
        std::exit(1);
    }
    if (conf.warmup < 0) {
        std::cerr << "Error: --warmup must be >= 0\n";
        std::exit(1);
    }
   // Validate kernel name (fail fast on unsupported kernels)
    if (conf.kernel != "copy"  &&
        conf.kernel != "scale" &&
        conf.kernel != "add"   &&
        conf.kernel != "triad" &&
        conf.kernel != "flops" &&
        conf.kernel != "fma"   &&
        conf.kernel != "stream") {
        std::cerr << "Error: unsupported --kernel '" << conf.kernel << "'\n";
        std::cerr << "Allowed kernels: copy, scale, add, triad, flops, fma\n";
        std::exit(1);
    }

    // If we reached here, parsing succeeded and config is valid
    return conf;
}

#endif // CONFIG_HPP
