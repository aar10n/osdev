#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly ZLIB_VERSION=1.2.11
readonly LIBPNG_VERSION=1.6.37
readonly FREETYPE_VERSION=2.10.0
readonly HARFBUZZ_VERSION=2.9.1

# build zlib
# ==========
# env:
#   ZLIB_BUILD_DIR   - build directory (default = <BUILD_DIR>/zlib)
#   ZLIB_INSTALL_DIR - install directory (default = <SYS_ROOT>/usr)
#   ZLIB_SYSROOT     - sysroot override (default = <SYS_ROOT>)
#
# example:
#   toolchain::zlib::build
toolchain::zlib::build() {
  local zlib="zlib-${GCC_VERSION}"
  local sysroot="${ZLIB_SYSROOT:-${SYS_ROOT}}"
  local build_dir=${ZLIB_BUILD_DIR:-${BUILD_DIR}/zlib}
  local install_dir=${ZLIB_INSTALL_DIR:-${SYS_ROOT}/usr}

  local options="--target=${target} --prefix=${install_dir} \
    --disable-shared --enable-static"
}

