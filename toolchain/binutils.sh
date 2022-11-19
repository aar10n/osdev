#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly BINUTILS_VERSION=2.38
readonly LIBTOOL_VERSION=2.4.6

# args:
#   <1>: target triplet
#   <2>: prefix
toolchain::binutils::install_fixup() {
  toolchain::util::check_args 2 $@

  local target_triple="$1"
  local prefix="$2"
  shift 2

  mkdir -p ${sysroot}/usr
  mkdir -p ${sysroot}/usr/bin
  mkdir -p ${sysroot}/usr/include
  pushd ${sysroot}/usr/bin
    for file in ../../${target_triple}/bin/*; do
      link=$(realpath --relative-to=$PWD $file)
#      ln -sf ${link} $(basename $file)
      ln -sf ${link} ${target_triple}-$(basename $file)
#      echo -e "-> ${CYAN}ln -sf ${link} $(basename $file)${RESET}"
    done
  popd
}

# build binutils
# ==============
# args:
#   <1>: target arch (i.e. 'x86_64')
#   <2>: build kind [kernel|system]
# env:
#   BINUTILS_BUILD_DIR   - build directory (default = <BUILD_DIR>/binutils)
#   BINUTILS_SYSROOT     - binutils sysroot (default = <SYS_ROOT>)
#   BINUTILS_INSTALL_DIR - install directory (default = <SYS_ROOT>)
#
# example:
#   toolchain::binutils::build x86_64
toolchain::binutils::build() {
  toolchain::util::check_args 2 $@

  local arch="$1"
  local build_kind="$2"
  local target_triple=""
  local targets=""
  shift 2
  case "$arch" in
    x86_64* | X64)
      arch="x86_64"
      target_triple="x86_64-osdev"
      targets="x86_64-elf" # ,x86_64-pe
      ;;
    *)
      toolchain::util::error "unsupported arch: $arch"
      exit 1
      ;;
  esac

  local binutils="binutils-${BINUTILS_VERSION}"
  local build_dir=${BINUTILS_BUILD_DIR:-${BUILD_DIR}/binutils}
  local sysroot=${BINUTILS_SYSROOT:-${SYS_ROOT}}
  local install_dir=${BINUTILS_INSTALL_DIR:-${sysroot}}
  local build_subdir=""
  case "${build_kind}" in
    kernel)
      install_dir=${BINUTILS_INSTALL_DIR:-${sysroot}}
      build_subdir="build-kernel"
      ;;
    system)
      install_dir=${BINUTILS_INSTALL_DIR:-${sysroot}/usr}
      build_subdir="build-system"
      ;;
    *)
      toolchain::util::error "unsupported build kind: ${build_kind}"
      exit 1
      ;;
  esac

  local options=(
    --target=${target_triple}
    --prefix=${install_dir}
    --srcdir=${build_dir}/src
    --with-sysroot=${sysroot}

    --enable-targets=${targets}
    --disable-werror
    # --disable-nls
  )

  mkdir -p ${build_dir}
  mkdir -p ${install_dir}
  mkdir -p ${build_dir}/${build_subdir}
  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src

      wget -nc http://ftp.gnu.org/gnu/binutils/${binutils}.tar.gz
      tar -xf ${binutils}.tar.gz -C src --strip-components=1
      patch -p1 -d src < ${PROJECT_DIR}/toolchain/patches/${binutils}.patch
    fi

    pushd ${build_subdir}
      toolchain::util::configure_step ../src/configure "${options[@]}"
      toolchain::util::build_step ${MAKE_j} "all-binutils all-gas all-ld"
      toolchain::util::install_step ${MAKE} "install-binutils install-gas install-ld"
    popd
  popd
}

# build libtool
# =============
# args:
#   <1>: target arch (i.e. 'x86_64')
# env:
#   LIBTOOL_BUILD_DIR   - build directory (default = <BUILD_DIR>/libtool)
#   LIBTOOL_SYSROOT     - libtool sysroot (default = <SYS_ROOT>)
#   LIBTOOL_INSTALL_DIR - install directory (default = <SYS_ROOT>)
#
# example:
#   toolchain::libtool::build x86_64
toolchain::libtool::build() {
  toolchain::util::check_args 1 $@

  local arch="$1"
  local target_triple=""
  shift 1
  case "$arch" in
    x86_64* | X64)
      arch="x86_64"
      target_triple="x86_64-elf"
      ;;
    *)
      toolchain::util::error "unsupported arch: $arch"
      exit 1
      ;;
  esac

  local version="${LIBTOOL_VERSION}"
  local build_dir=${LIBTOOL_BUILD_DIR:-${BUILD_DIR}/libtool}
  local sysroot=${LIBTOOL_SYSROOT:-${SYS_ROOT}}
  local install_dir=${LIBTOOL_INSTALL_DIR:-${sysroot}/usr}
  local tool_prefix="${sysroot}/usr/bin/${arch}-osdev-"

  local options=(
    --host="${arch}"
    --prefix=${install_dir}
    --with-sysroot=${sysroot}
    --program-prefix="${arch}-osdev-"
  )

  mkdir -p ${build_dir}
  mkdir -p ${install_dir}
  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src
      wget -nc https://ftpmirror.gnu.org/libtool/libtool-${version}.tar.gz
      tar -xf libtool-${version}.tar.gz -C src --strip-components=1
    fi

    mkdir -p build
    pushd build
      export CC=${tool_prefix}gcc
      export RANLIB=${tool_prefix}ranlib
      toolchain::util::configure_step ../src/configure "${options[@]}"
      make
      make install
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
      toolchain::binutils::build $@
      ;;
    build-libtool)
      toolchain::libtool::build $@
      ;;
    *)
      toolchain::util::error "unsupported command '${command}'"
      exit 1
      ;;
  esac
fi
