#!/bin/bash
set -e
shopt -s nullglob

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly LIBDWARF_VERSION=0.5.0


# build libdwarf
# ==============
# args:
#   <1>: target arch (i.e. 'x86_64')
#
# env:
#   LIBDWARF_BUILD_DIR   - build directory (default = <BUILD_DIR>/libdwarf)
#   LIBDWARF_SYSROOT     - sysroot override (default = <TOOL_ROOT>)
#   LIBDWARF_INSTALL_DIR - install override (default = <TOOL_ROOT>)
#
# example:
#   toolchain::libdwarf::build
toolchain::libdwarf::build() {
  toolchain::util::check_args 1 $@

  local arch="$1"
  shift 1
  case "$arch" in
    x86_64* | X64)
      arch="x86_64"
      ;;
    *)
      toolchain::util::error "unsupported arch: $arch"
      exit 1
      ;;
  esac

  local version="${LIBDWARF_VERSION}"
  local build_dir=${LIBDWARF_BUILD_DIR:-${BUILD_DIR}/libdwarf}
  local sysroot="${LIBDWARF_SYSROOT:-${TOOL_ROOT}}"
  local install_dir=${LIBDWARF_INSTALL_DIR:-${TOOL_ROOT}}
#  local tool_prefix="${sysroot}/bin/${arch}-elf-"
  local tool_prefix="${BUILD_DIR}/sysroot/usr/bin/${arch}-osdev-"

  mkdir -p ${build_dir}
  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src
      wget -nc https://github.com/davea42/libdwarf-code/releases/download/v${version}/libdwarf-${version}.tar.xz
      tar -xf libdwarf-${version}.tar.xz -C src --strip-components=1
      patch -d src < ${PROJECT_DIR}/toolchain/patches/libdwarf.patch
    fi

    mkdir -p build
    pushd build
      export CHOST="${arch}"
      export CFLAGS="-fPIC"
      export CC="${tool_prefix}gcc"
      export AR="${tool_prefix}gcc-ar"
      export RANLIB="${tool_prefix}gcc-ranlib"

      ../src/configure \
        --host=${arch}-elf \
        --prefix=${install_dir} \
        --with-sysroot=${sysroot} \
        --with-pic \
        --disable-libelf

      ${MAKE_j}
      ${MAKE} install

      ${tool_prefix}objcopy \
          --redefine-sym malloc=kmalloc \
          --redefine-sym calloc=kcalloc \
          --redefine-sym free=kfree \
          --redefine-sym printf=kprintf \
          --redefine-sym realloc=__debug_realloc_stub \
          --redefine-sym fclose=__debug_fclose_stub \
          --redefine-sym getcwd=__debug_getcwd_stub \
          --redefine-sym do_decompress_zlib=__debug_do_decompress_zlib_stub \
          --redefine-sym uncompress=__debug_uncompress_stub \
        src/lib/libdwarf/.libs/libdwarf.a ${BUILD_DIR}/libdwarf.a
      ${tool_prefix}strip -g ${BUILD_DIR}/libdwarf.a
    popd
  popd
}

#
# main
#

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  if [[ $# -eq 0 ]]; then
    exit 1
  fi

  command="$1"
  shift
  case "${command}" in
    build)
      toolchain::libdwarf::build $@
      ;;
    *)
      toolchain::util::error "unsupported command '${command}'"
      exit 1
      ;;
  esac
fi
