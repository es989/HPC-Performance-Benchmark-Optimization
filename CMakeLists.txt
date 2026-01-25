# minimal CMake version required to build the project
cmake_minimum_required(VERSION 3.10)

# giving a name to the project
project(HPC_Benchmarking)

# using C++ 2017 standard for modern features (e.g., std::chrono, filesystem, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------- Project Build Target -------------------

# Define the main executable and its source files
# This creates the 'bench' binary from the main entry point
add_executable(bench src/main.cpp)

# ------------- Project Structure Configuration ---------

# Add 'include' directory to the search path for header files (e.g., timer.hpp)
# Scoped to 'bench' only (does not affect other targets)
target_include_directories(bench PRIVATE include)

# ------------- Compiler Flags / Optimization -----------

# Enable common warnings (code quality) for all build types
target_compile_options(bench PRIVATE -Wall)

# Release Mode: Maximum performance
# -O3         : aggressive optimizations (unrolling, inlining, vectorization)
# -march=native : tune code generation for the current CPU (enables AVX/AVX2/AVX-512 when available)
target_compile_options(bench PRIVATE
  $<$<CONFIG:Release>:-O3 -march=native>
)

# Debug Mode: Transparency / easier debugging
# -O0 : disable optimizations (baseline + easier to debug)
# -g  : add debug symbols (maps machine code back to source lines)
target_compile_options(bench PRIVATE
  $<$<CONFIG:Debug>:-O0 -g>)
