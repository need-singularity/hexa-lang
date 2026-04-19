# lib/hxnccl/CMakeLists.txt — libhxnccl.so build for ALM r12 FSDP B1 unblock.
#
# Two modes:
#
#   Mac stub (no NCCL/CUDA): used for single-rank smoke + ABI verification.
#     cmake -S lib/hxnccl -B lib/hxnccl/build
#     cmake --build lib/hxnccl/build
#
#   Linux Day-2 (real NCCL+CUDA): production multi-rank build for H100 SXM.
#     cmake -S lib/hxnccl -B lib/hxnccl/build -DHXNCCL_WITH_NCCL=ON \
#           -DCUDA_HOME=/usr/local/cuda -DNCCL_HOME=/usr/local/cuda
#     cmake --build lib/hxnccl/build
#
# Output:
#   lib/hxnccl/build/libhxnccl.so   (Linux)
#   lib/hxnccl/build/libhxnccl.dylib (Mac stub)
#
# This wraps self/native/hxccl_linux.c (which already carries the Day-2
# NCCL implementation behind -DHXCCL_NCCL=1) and exports the user-facing
# hxnccl_init_world / hxnccl_allreduce_f32 / hxnccl_broadcast_f32 /
# hxnccl_barrier / hxnccl_destroy_world surface required by
# training/nccl_ffi.hexa for ALM r12 FSDP halvening.

cmake_minimum_required(VERSION 3.16)
project(hxnccl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(HXNCCL_WITH_NCCL "Link against real NCCL + CUDA runtime" OFF)

# Source files: thin wrapper + the existing hxccl shim.
set(HXNCCL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/hxnccl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../self/native/hxccl_linux.c
)

add_library(hxnccl SHARED ${HXNCCL_SOURCES})

# Common compile flags.
target_compile_options(hxnccl PRIVATE -O3 -Wall)
if(NOT APPLE)
    target_compile_options(hxnccl PRIVATE -march=native)
endif()

if(HXNCCL_WITH_NCCL)
    if(APPLE)
        message(FATAL_ERROR "HXNCCL_WITH_NCCL=ON requires Linux (NCCL is not available on macOS)")
    endif()

    # CUDA / NCCL header + lib paths. Defaults match runpod/pytorch image.
    if(NOT DEFINED CUDA_HOME)
        set(CUDA_HOME "/usr/local/cuda")
    endif()
    if(NOT DEFINED NCCL_HOME)
        set(NCCL_HOME "${CUDA_HOME}")
    endif()

    if(NOT EXISTS "${CUDA_HOME}/include/cuda_runtime.h")
        message(FATAL_ERROR "cuda_runtime.h not found at ${CUDA_HOME}/include — set -DCUDA_HOME=...")
    endif()

    # nccl.h may live in NCCL_HOME/include or /usr/include or /usr/local/include
    find_path(NCCL_INC nccl.h
              HINTS "${NCCL_HOME}/include" /usr/include /usr/local/include
              REQUIRED)
    message(STATUS "[hxnccl] NCCL include: ${NCCL_INC}")
    message(STATUS "[hxnccl] CUDA_HOME: ${CUDA_HOME}")

    target_compile_definitions(hxnccl PRIVATE HXCCL_NCCL=1)
    target_include_directories(hxnccl PRIVATE
                               "${CUDA_HOME}/include"
                               "${NCCL_INC}")
    target_link_directories(hxnccl PRIVATE
                            "${CUDA_HOME}/lib64"
                            "${NCCL_HOME}/lib")
    target_link_libraries(hxnccl PRIVATE nccl cudart)
else()
    message(STATUS "[hxnccl] STUB MODE — no NCCL/CUDA. Single-rank only.")
endif()

# Smoke test executable: always built, exercises init/barrier/destroy.
add_executable(hxnccl_smoke ${CMAKE_CURRENT_SOURCE_DIR}/smoke.c)
target_link_libraries(hxnccl_smoke PRIVATE hxnccl)

# Install rule (so `make install` lands in CMAKE_INSTALL_PREFIX/lib).
include(GNUInstallDirs)
install(TARGETS hxnccl
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
