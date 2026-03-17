# skate3 - ReXGlue Recompiled Project
#
# This file is yours to edit. 'rexglue migrate' will NOT overwrite it.
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(skate3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable SSSE3 for SIMD optimizations
add_compile_options(-mssse3)

# Disable FidelityFX for faster build
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "Enable FidelityFX" FORCE)

include(generated/rexglue.cmake)

# Sources
set(SKATE3_SOURCES
    src/main.cpp
    src/gpu_hooks.cpp
)

if(WIN32)
    add_executable(skate3 WIN32 ${SKATE3_SOURCES})
else()
    add_executable(skate3 ${SKATE3_SOURCES})
endif()

rexglue_setup_target(skate3)

