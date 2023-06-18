#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"
source "${PROJECT_DIR}/toolchain/mlibc.sh"

#----------------------------------

readonly GCC_VERSION=12.1.0


# build and install gcc
# =====================
# args:
#   <1>: target arch (i.e. 'x86_64')
#   <2>: build kind [kernel|system]
#
# env:
#   GCC_BUILD_DIR   - build directory (default = <BUILD_DIR>/gcc)
#   GCC_SYSROOT     - gcc sysroot (default = <SYS_ROOT>)
#   GCC_INSTALL_DIR - install directory (default = <TOOL_ROOT>)
#
# example:
#   toolchain::gcc:build x86_64 kernel
#   toolchain::gcc:build x86_64 system
toolchain::gcc:build() {
  toolchain::util::check_args 2 $@

  local arch="$1"
  local build_kind="${2:-normal}"
  shift 2
  case "$arch" in
    x86_64* | X64)
      arch="x86_64"
      ;;
    *)
      toolchain::util::error "unsupported arch: $arch"
      exit 1
      ;;
  esac

  local gcc="gcc-${GCC_VERSION}"
  local build_dir=${GCC_BUILD_DIR:-${BUILD_DIR}/gcc}
  local sysroot=${GCC_SYSROOT:-${SYS_ROOT}}
  local install_dir=${GCC_INSTALL_DIR:-${TOOL_ROOT}}

  local target_triple=""
  local build_subdir=""
  case "$build_kind" in
    kernel)
      build_subdir="build-kernel"
      target_triple="${arch}-elf"
      ;;
    system)
      build_subdir="build-system"
      target_triple="${arch}-osdev"
      ;;
    *)
      toolchain::util::error "invalid build kind: $build_kind"
      exit 1
      ;;
  esac

  local options=(
    --target=${target_triple}
    --prefix=${install_dir}
    --srcdir=${build_dir}/src
    --with-sysroot=${sysroot}

    --enable-languages=c,c++
    --disable-werror
    # --disable-nls
    --enable-initfini-array
  )

  local steps=()
  if [ ${build_kind} == "kernel" ]; then
    options+=(
      --disable-hosted-libstdcxx
      --disable-shared
      "--enable-gnu-indirect-function"
    )

    # kernel gcc steps
    configure_opts="${options[@]}"
    steps+=(
      # gcc
      "toolchain::util::configure_step ../src/configure ${configure_opts}"
      "toolchain::util::build_step ${MAKE_j} inhibit_libc=true all-gcc"
      "toolchain::util::install_step ${MAKE} install-gcc"
      # libgcc
      "toolchain::util::build_step ${MAKE_j} inhibit_libc=true all-target-libgcc"
      "toolchain::util::install_step ${MAKE} install-target-libgcc"
    )
  elif [ ${build_kind} == "system" ]; then
    options+=(
      --includedir=${sysroot}/include
      --libdir=${sysroot}/lib
      --disable-multilib
      --enable-libstdcxx-filesystem-ts
    )

    # system gcc steps
    configure_opts="${options[@]}"
    steps+=(
      # mlibc headers
      "toolchain::util::configure_step toolchain::mlibc::build ${arch} headers"
      # gcc
      "toolchain::util::configure_step ../src/configure ${configure_opts}"
      "toolchain::util::build_step ${MAKE_j} all-gcc"
      "toolchain::util::install_step ${MAKE} install-gcc"
      # cxxshim + frigg
      "toolchain::util::install_step toolchain::cxxshim::install"
      "toolchain::util::install_step toolchain::frigg::install"
      # mlibc
      "toolchain::util::build_step toolchain::mlibc::build ${arch} lib"
      # libgcc
      "toolchain::util::build_step ${MAKE_j} all-target-libgcc"
      "toolchain::util::install_step ${MAKE} install-target-libgcc"
      # libstdc++
      "toolchain::util::build_step ${MAKE_j} all-target-libstdc++-v3"
      "toolchain::util::install_step ${MAKE} install-target-libstdc++-v3"
    )

    export MLIBC_SYSROOT=${sysroot}
    export MLIBC_INSTALL_DIR=${install_dir}
    export MLIBC_CROSS_PREFIX=${sysroot}/usr/bin/${target_triple}-
  fi

  mkdir -p ${install_dir}
  mkdir -p ${build_dir}/${build_subdir}

  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src

      wget -nc https://ftp.gnu.org/gnu/gcc/${gcc}/${gcc}.tar.gz
      tar -xf ${gcc}.tar.gz -C src --strip-components=1
      patch -p1 -d src < ${PROJECT_DIR}/toolchain/patches/${gcc}.patch

      # download gmp, mpfr, mpc and isl
      pushd src
        ./contrib/download_prerequisites

        pushd gcc
          autoconf
        popd
        pushd libgcc
          autoconf
        popd
        pushd libstdc++-v3
          autoconf
        popd
      popd
    fi
  popd

  # execute steps
  pushd ${build_dir}/${build_subdir}
    for step in "${steps[@]}"; do
      echo -e "-> ${CYAN}${step}${RESET}"
      ${step}
    done
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
      toolchain::gcc:build $@
      ;;
    *)
      toolchain::util::error "unsupported command '${command}'"
      exit 1
      ;;
  esac
fi
