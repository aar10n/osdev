#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly GCC_VERSION=12.1.0

# build and install gcc
# =====================
# args:
#   <1>: target triplet
#   <2>: component [all|gcc|libgcc|libstdc++]
#
# env:
#   GCC_BUILD_DIR   - build directory (default = <BUILD_DIR>/gcc)
#   GCC_INSTALL_DIR - install directory (default = <BUILD_DIR>/toolchain)
#   GCC_SYSROOT     - sysroot override
#   GCC_OPTIONS     - override configure options
#
# example:
#   toolchain::gcc:build x86_64-elf all
#   toolchain::gcc:build x86_64-elf-osdev gcc
toolchain::gcc:build() {
  toolchain::util::check_args 2 $@

  local gcc="gcc-${GCC_VERSION}"
  local target=$1
  local build_dir=${GCC_BUILD_DIR:-${BUILD_DIR}/gcc}
  local install_dir=${GCC_INSTALL_DIR:-${BUILD_DIR}/toolchain}
  local options="--target=${target} --prefix=${install_dir} \
      ${GCC_SYSROOT:+--with-sysroot=${GCC_SYSROOT}} --enable-languages=c,c++ \
      ${GCC_OPTIONS:-"--disable-nls --disable-werror --disable-multilib --enable-initfini-array"}"

  local make_build_target=""
  local make_install_target=""
  case $2 in
    all)
      make_build_target="all"
      make_install_target="install"
      ;;
    gcc)
      make_build_target="all-gcc"
      make_install_target="install-gcc"
      ;;
    libgcc)
      make_build_target="all-target-libgcc"
      make_install_target="install-target-libgcc"
      ;;
    libstdc++)
      make_build_target="all-target-libstdc++-v3"
      make_install_target="install-target-libstdc++-v3"
      ;;
    *)
      echo "unknown component: $2"
      exit 1
      ;;
  esac

  mkdir -p ${build_dir}
  mkdir -p ${install_dir}

  pushd ${build_dir}
    if [[ ! -d src ]]; then
      mkdir -p src
      mkdir -p build

      wget -nc https://ftp.gnu.org/gnu/gcc/${gcc}/${gcc}.tar.gz
      tar -xf ${gcc}.tar.gz -C src --strip-components=1
      patch -p1 -d src < ${PROJECT_DIR}/toolchain/patches/${gcc}.patch
      rm -rf ${gcc}.tar.gz

      # download gmp, mpfr, mpc and isl
      pushd src
        ./contrib/download_prerequisites
      popd
    fi

    pushd build
      toolchain::util::configure ../src/configure "${options}"
      toolchain::util::make_build ${make_build_target}
      toolchain::util::make_install ${make_install_target}
    popd
  popd
}
