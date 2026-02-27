# OpenWrt cross-compile toolchain for aarch64_cortex-a53 (musl libc, gcc-13)
#
# Usage:
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-openwrt.cmake
#
# The toolchain is expected at <project-root>/cross-toolchain/ (populated by
# `make cross-setup`).  The path is derived from CMAKE_CURRENT_LIST_DIR so
# it resolves correctly even inside CMake's try_compile() sub-projects where
# CMAKE_SOURCE_DIR points to the temporary build directory.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# CMAKE_CURRENT_LIST_DIR is always the directory of this file (cmake/).
# Go one level up to reach the project root, then into cross-toolchain/.
get_filename_component(_CROSS_BASE "${CMAKE_CURRENT_LIST_DIR}/../cross-toolchain" REALPATH)

set(_TC  "${_CROSS_BASE}/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl")
set(_TGT "${_CROSS_BASE}/target-aarch64_cortex-a53_musl")

set(CMAKE_C_COMPILER   "${_TC}/bin/aarch64-openwrt-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${_TC}/bin/aarch64-openwrt-linux-musl-g++")
set(CMAKE_STRIP        "${_TC}/bin/aarch64-openwrt-linux-musl-strip")

# Do NOT set CMAKE_SYSROOT to the package staging dir.
# The cross-compiler already knows where its musl C library headers are
# (toolchain-*/aarch64-openwrt-linux-musl/sys-include/).  Pointing --sysroot
# at the package staging dir (which has no libc headers) causes it to fall
# through to the host /usr/include (glibc), breaking everything.
#
# Instead: pass the package staging dir as explicit -I/-L flags so that
# libcurl, libnl, etc. are found, while the compiler's built-in musl headers
# remain in effect.
set(CMAKE_C_FLAGS_INIT             "-I${_TGT}/usr/include")
set(CMAKE_CXX_FLAGS_INIT           "-I${_TGT}/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-L${_TGT}/usr/lib -Wl,-rpath-link,${_TGT}/usr/lib")

# Tell CMake's find_* machinery to look in the staging dir for headers/libs.
set(CMAKE_FIND_ROOT_PATH "${_TGT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config: use cross-compiled libraries in the staging dir
set(ENV{PKG_CONFIG_DIR}         "")
set(ENV{PKG_CONFIG_LIBDIR}      "${_TGT}/usr/lib/pkgconfig:${_TGT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${_TGT}")
