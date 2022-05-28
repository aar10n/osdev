#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly BINUTILS_VERSION=2.38

# build and install binutils
# ==========================
# args:
#   <1>: target triplet
# env:
#   BINUTILS_BUILD_DIR   - build directory (default = <BUILD_DIR>/binutils)
#   BINUTILS_INSTALL_DIR - install directory (default = <BUILD_DIR>/toolchain)
#   BINUTILS_SYSROOT     - sysroot override
#   BINUTILS_OPTIONS     - override configure options
#
# example:
#   toolchain::binutils::build x86_64-elf
#   toolchain::binutils::build x86_64-elf-osdev
toolchain::binutils::build() {
  toolchain::util::check_args 1 $@

  local binutils="binutils-${BINUTILS_VERSION}"
  local target=$1
  local build_dir=${BINUTILS_BUILD_DIR:-${BUILD_DIR}/binutils}
  local install_dir=${BINUTILS_INSTALL_DIR:-${BUILD_DIR}/toolchain}
  local options="--target=${target} --prefix=${install_dir} \
    ${BINUTILS_SYSROOT:+--with-sysroot=${BINUTILS_SYSROOT}} \
    ${BINUTILS_OPTIONS:-"--disable-nls --disable-werror"}"

  mkdir -p ${build_dir}
  mkdir -p ${install_dir}

  pushd ${build_dir}
    if [[ ! -d src ]]; then
      mkdir -p src
      mkdir -p build

      wget -nc http://ftp.gnu.org/gnu/binutils/${binutils}.tar.gz
      tar -xf ${binutils}.tar.gz -C src --strip-components=1
      patch -p1 -d src < ${PROJECT_DIR}/toolchain/patches/${binutils}.patch
      rm -rf ${binutils}.tar.gz
    fi

    pushd build
      toolchain::util::configure ../src/configure "${options}"
      toolchain::util::make_build all
      toolchain::util::make_install install-strip
    popd
  popd
}
