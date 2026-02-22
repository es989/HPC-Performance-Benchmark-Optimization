# ------------------------------------------------------------
# Minimal CMake version required to build the project
# ------------------------------------------------------------
cmake_minimum_required(VERSION 3.10)

# ------------------------------------------------------------
# Project name (used by IDEs, build systems, and package tools)
# ------------------------------------------------------------
project(HPC_Benchmarking)

# ------------------------------------------------------------
# C++ Standard
# We use C++17 for modern language features and a clean baseline.
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# IntelliSense / Tooling (Highly recommended for VS Code)
#
# This generates build/compile_commands.json which contains:
# - exact compiler command lines
# - include paths (-I...)
# - compile flags (-O3, -g, etc.)
#
# VS Code can read this database to resolve headers correctly
# and eliminate false "cannot open source file" squiggles.
# ------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------
# Build Target
#
# IMPORTANT:
# - You must list ALL .cpp files that contain compiled code.
# - If you forget a new .cpp (e.g., stream_sweep.cpp),
#   the project may fail at link time with "undefined reference".
# ------------------------------------------------------------
add_executable(bench
  src/main.cpp
  src/stream_sweep.cpp
  src/sys_info.cpp
)

# ------------------------------------------------------------
# Include Directories
#
# This tells the compiler where to search for project headers:
#   #include "timer.hpp"
#   #include "config.hpp"
#   #include "results.hpp"
#   #include "utils.hpp"
#   #include "stream_kernels.hpp"
#
# Using ${CMAKE_SOURCE_DIR}/include is robust for out-of-source builds:
# (i.e., when you build into a separate "build/" directory).
# ------------------------------------------------------------
target_include_directories(bench PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

# ------------------------------------------------------------
# Compiler Warnings
#
# -Wall enables a useful baseline of warnings (code quality).
# You can later add -Wextra -Wpedantic if you want stricter checks.
# ------------------------------------------------------------
target_compile_options(bench PRIVATE -Wall)

# ------------------------------------------------------------
# Optimization / Debug Flags (per build type)
#
# Release:
# -O3          : aggressive optimizations (inlining, unrolling, vectorization)
# -march=native: tune code generation for the current CPU (AVX/AVX2/AVX-512 if available)
#
# Debug:
# -O0 : disable optimizations (easier debugging + baseline comparisons)
# -g  : emit debug symbols (stack traces, source-level debugging)
# ------------------------------------------------------------
target_compile_options(bench PRIVATE
  $<$<CONFIG:Release>:-O3 -march=native>
  $<$<CONFIG:Debug>:-O0 -g>
)
