#include <iostream>
#include "config.hpp"
#include "results.hpp"

int main(int argc, char** argv) {
    // 1. קריאת הגדרות
    Config conf = parse_args(argc, argv);
    conf.print();

    // 2. יצירת אובייקט תוצאות והזנת נתוני דמה
    BenchmarkResult res;
    res.total_ns = 5000000000; // 5 שניות
    res.avg_ns = 12.5;
    res.bandwidth_gb_s = 64.2;
    res.gflops = 120.5;

    // 3. שמירה
    std::cout << "\nTesting JSON Export...\n";
    res.save(conf);

    return 0;
}