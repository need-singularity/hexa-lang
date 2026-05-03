# lib/hxpyembed/CMakeLists.txt — libhxpyembed shared library
#
# Embedded CPython FFI shim for stdlib/python_ffi.hexa. Links against
# libpython3.X (resolved via python3-config --embed --ldflags) and exports
# the hxpy_* surface declared in hxpyembed.c.
#
# Build:
#   cmake -S lib/hxpyembed -B lib/hxpyembed/build
#   cmake --build lib/hxpyembed/build
#
# Output:
#   lib/hxpyembed/build/libhxpyembed.dylib  (macOS)
#   lib/hxpyembed/build/libhxpyembed.so     (Linux)
#
# Uses python3-config to discover the include dir + link flags. If the
# user's `python3-config` is not on PATH, set PYTHON3_CONFIG explicitly:
#   cmake -DPYTHON3_CONFIG=/opt/homebrew/opt/python@3.14/bin/python3-config ...

cmake_minimum_required(VERSION 3.16)
project(hxpyembed C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── Python discovery via python3-config ───────────────────────────────────
if(NOT DEFINED PYTHON3_CONFIG)
    find_program(PYTHON3_CONFIG NAMES python3-config python-config REQUIRED)
endif()
message(STATUS "[hxpyembed] python3-config: ${PYTHON3_CONFIG}")

execute_process(
    COMMAND ${PYTHON3_CONFIG} --includes
    OUTPUT_VARIABLE PY_INCLUDES
    OUTPUT_STRIP_TRAILING_WHITESPACE)

# Prefer --embed (Python ≥ 3.8) so we link against libpython itself rather
# than the bare interpreter stub.
execute_process(
    COMMAND ${PYTHON3_CONFIG} --ldflags --embed
    OUTPUT_VARIABLE PY_LDFLAGS
    RESULT_VARIABLE PY_EMBED_RC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(NOT PY_EMBED_RC EQUAL 0)
    message(STATUS "[hxpyembed] python3-config --ldflags --embed failed; falling back to --ldflags")
    execute_process(
        COMMAND ${PYTHON3_CONFIG} --ldflags
        OUTPUT_VARIABLE PY_LDFLAGS
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

message(STATUS "[hxpyembed] PY_INCLUDES: ${PY_INCLUDES}")
message(STATUS "[hxpyembed] PY_LDFLAGS:  ${PY_LDFLAGS}")

# ── Library target ─────────────────────────────────────────────────────────
add_library(hxpyembed SHARED ${CMAKE_CURRENT_SOURCE_DIR}/hxpyembed.c)

# Translate the include flags string into target_include_directories form.
separate_arguments(PY_INC_LIST UNIX_COMMAND "${PY_INCLUDES}")
foreach(flag ${PY_INC_LIST})
    if(flag MATCHES "^-I(.*)$")
        target_include_directories(hxpyembed PRIVATE "${CMAKE_MATCH_1}")
    endif()
endforeach()

# Pass --ldflags through verbatim — it already contains -L/-l/-framework.
separate_arguments(PY_LDFLAGS_LIST UNIX_COMMAND "${PY_LDFLAGS}")
target_link_options(hxpyembed PRIVATE ${PY_LDFLAGS_LIST})

target_compile_options(hxpyembed PRIVATE -O2 -Wall -Wextra)

if(APPLE)
    # Ensure the dylib has an install name that lets dlopen find it via
    # @rpath / DYLD_LIBRARY_PATH, matching the hxnccl convention.
    set_target_properties(hxpyembed PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# Install rule — symmetric with hxnccl (raw#181 install/uninstall pairing).
include(GNUInstallDirs)
install(TARGETS hxpyembed
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# C-side smoke executable — exercises the FFI surface end-to-end.
add_executable(hxpyembed_smoke ${CMAKE_CURRENT_SOURCE_DIR}/smoke.c)
target_link_libraries(hxpyembed_smoke PRIVATE hxpyembed)
if(APPLE)
    set_target_properties(hxpyembed_smoke PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}")
endif()

if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -E echo
                "Uninstall: rm files listed in ${CMAKE_BINARY_DIR}/install_manifest.txt"
        VERBATIM)
endif()
