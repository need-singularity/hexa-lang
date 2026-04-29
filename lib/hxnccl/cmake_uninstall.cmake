# cmake_uninstall.cmake — invoked by the `uninstall` custom target in
# lib/hxnccl/CMakeLists.txt. Reads the manifest CMake auto-writes during
# `make install` and removes each path. Idempotent: missing entries are
# warned, not errored.
#
# Usage (driven by Makefile target):
#   cmake -DMANIFEST=<build>/install_manifest.txt -P cmake_uninstall.cmake
#
# raw#181 (asymmetric build script) compliance — pairs with install(TARGETS).

if(NOT DEFINED MANIFEST)
    set(MANIFEST "${CMAKE_BINARY_DIR}/install_manifest.txt")
endif()

if(NOT EXISTS "${MANIFEST}")
    message(WARNING "[hxnccl uninstall] manifest not found: ${MANIFEST} — nothing to remove (was `make install` ever run?)")
    return()
endif()

file(STRINGS "${MANIFEST}" _files)
foreach(_f IN LISTS _files)
    if(EXISTS "${_f}" OR IS_SYMLINK "${_f}")
        message(STATUS "[hxnccl uninstall] removing ${_f}")
        file(REMOVE "${_f}")
    else()
        message(STATUS "[hxnccl uninstall] already absent: ${_f}")
    endif()
endforeach()
