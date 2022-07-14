#!/bin/bash
set -e
shopt -s nullglob

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly FRIGG_SHA=0b2a99ba13df63ceb4b53b85e2b80d40cbff3cab
readonly CXXSHIM_SHA=5bdbdc73a235042376f467691f07397ae40b7542

readonly FRIGG_CROSS_FILE=$(cat <<-'EOF'
[binaries]
c = '${CROSS_PREFIX}gcc'
cpp = '${CROSS_PREFIX}g++'
ar = '${CROSS_PREFIX}ar'
strip = '${CROSS_PREFIX}strip'
pkgconfig = 'pkg-config'

[host_machine]
system = 'osdev'
cpu_family = '${ARCH}'
cpu = '${ARCH}'
endian = 'little'
EOF
)


# install cxxshim library
# =======================
# env:
#   CXXSHIM_BUILD_DIR   - build directory (default = <BUILD_DIR>/cxxshim)
#   CXXSHIM_SYSROOT     - cxxshim destination sysroot (default = <SYS_ROOT>)
#   CXXSHIM_INSTALL_DIR - install directory (default = <SYS_ROOT>/usr)
toolchain::cxxshim::install() {
  local build_dir=${CXXSHIM_BUILD_DIR:-${BUILD_DIR}/cxxshim}
  local sysroot=${CXXSHIM_SYSROOT:-${SYS_ROOT}}
  local install_dir=${CXXSHIM_INSTALL_DIR:-${sysroot}/usr}

  local options=(
    --prefix=${install_dir}
    --libdir=lib
    --includedir=share/cxxshim/include
    --buildtype=debugoptimized
    -Dinstall_headers=true
  )

  mkdir -p ${build_dir}
  mkdir -p ${build_dir}/build
  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src
      curl -L https://github.com/managarm/cxxshim/archive/${CXXSHIM_SHA}.tar.gz --output cxxshim.tar.gz
      tar -xf cxxshim.tar.gz -C src --strip-components=1
    fi

    pushd src
      echo -e "-> ${CYAN}toolchain::util::configure_step meson ${options[@]} ${build_dir}/build${RESET}"
      toolchain::util::configure_step meson ${options[@]} ${build_dir}/build
    popd
    pushd build
      echo -e "->  ${CYAN}toolchain::util::build_step ninja${RESET}"
      toolchain::util::build_step ninja
      echo -e "-> ${CYAN}toolchain::util::install_step ninja install${RESET}"
      toolchain::util::install_step ninja install
    popd
  popd
}

# install frigg library
# =====================
# env:
#   FRIGG_BUILD_DIR   - build directory (default = <BUILD_DIR>/frigg)
#   FRIGG_SYSROOT     - frigg destination sysroot (default = <SYS_ROOT>)
#   FRIGG_INSTALL_DIR - install directory (default = <SYS_ROOT>/usr)
toolchain::frigg::install() {
  local build_dir=${FRIGG_BUILD_DIR:-${BUILD_DIR}/frigg}
  local sysroot=${FRIGG_SYSROOT:-${SYS_ROOT}}
  local install_dir=${FRIGG_INSTALL_DIR:-${sysroot}/usr}

  local options=(
    --prefix=${install_dir}
    --libdir=lib
    --includedir=share/frigg/include
    --buildtype=debugoptimized
    -Dbuild_tests=disabled
  )

  mkdir -p ${build_dir}
  mkdir -p ${build_dir}/build
  pushd ${build_dir}
    if [ ! -d src ]; then
      mkdir -p src
      curl -L https://github.com/managarm/frigg/archive/${FRIGG_SHA}.tar.gz --output frigg.tar.gz
      tar -xf frigg.tar.gz -C src --strip-components=1
    fi

    pushd src
      echo -e "-> ${CYAN}toolchain::util::configure_step meson ${options[@]} ${build_dir}/build${RESET}"
      toolchain::util::configure_step meson ${options[@]} ${build_dir}/build
    popd
    pushd build
      echo -e "->  ${CYAN}toolchain::util::build_step ninja${RESET}"
      toolchain::util::build_step ninja
      echo -e "-> ${CYAN}toolchain::util::install_step ninja install${RESET}"
      toolchain::util::install_step ninja install
    popd
  popd
}


# clone and setup mlibc
# =====================
# env:
#   MLIBC_DIR - mlibc directory (default = <PROJECT_DIR>/third-party/mlibc)
#
# example:
#   toolchain::mlibc::setup
toolchain::mlibc::setup() {
  local mlibc_dir=${MLIBC_DIR:-${PROJECT_DIR}/third-party/mlibc}

  if [ ! -d ${mlibc_dir} ]; then
    pushd ${PROJECT_DIR}
      git submodule update --init --recursive
    popd
  fi

  # no more patching we're using a fork instead
  #  patch -p1 -d ${mlibc_dir} < ${PROJECT_DIR}/toolchain/patches/mlibc.patch

#  # fix symlinks
#  pushd ${mlibc_dir}/sysdeps/osdev/include
#    for dir in */; do
#      pushd ${dir}
#        for file in *.h; do
#          link=$(cat $file)
#          rm $file
#          ln -s $link $(basename $file)
#        done
#      popd
#    done
#  popd
#
#  # link default abi headers
#  pushd ${mlibc_dir}/sysdeps/osdev/include/abi-bits
#    for file in ../../../../abis/mlibc/*.h; do
#      if [ ! -f $(basename $file) ]; then
#        ln -s $file $(basename $file)
#      fi
#    done
#  popd
}

# build mlibc standard library
# ============================
# args:
#   <1>: target arch (i.e. 'x86_64')
#   <2>: component [lib|headers]
# env:
#   MLIBC_DIR          - mlibc directory (default = <PROJECT_DIR>/third-party/mlibc)
#   MLIBC_BUILD_TYPE   - build type [RELEASE|DEBUG] (default = DEBUG)
#   MLIBC_BUILD_DIR    - mlibc build directory (default = <BUILD_DIR>/mlibc/build)
#   MLIBC_SYSROOT      - mlibc destination sysroot (default = <SYS_ROOT>)
#   MLIBC_INSTALL_DIR  - install directory (default = <SYS_ROOT>/usr)
#   MLIBC_CROSS_PREFIX - cross-toolchain prefix (default = <SYS_ROOT>/usr/bin/<ARCH>-osdev-)
#
# example:
#   toolchain::mlibc::build x86_64 lib
#   toolchain::mlibc::build x86_64 headers
toolchain::mlibc::build() {
  toolchain::util::check_args 2 $@

  local mlibc_dir=${MLIBC_DIR:-${PROJECT_DIR}/third-party/mlibc}
  local build_dir=${MLIBC_BUILD_DIR:-${BUILD_DIR}/mlibc/build}
  local build_type=$(echo "${MLIBC_BUILD_TYPE:-debug}" | tr '[:upper:]' '[:lower:]')
  local sysroot=${MLIBC_SYSROOT:-${SYS_ROOT}}
  local install_dir=${MLIBC_INSTALL_DIR:-${sysroot}/usr}
  if [ "$build_type" != "debug" ] && [ "$build_type" != "release" ]; then
    toolchain::util::error "invalid build type: $build_type"
  fi

  local arch="$1"
  local component="$2"
  shift 2
  case "$arch" in
    x86_64* | X64)
      arch="x86_64"
      ;;
    *)
      toolchain::util::error "unsupported arch: $1"
      exit 1
      ;;
  esac

  local cross_prefix=${MLIBC_CROSS_PREFIX:-${sysroot}/usr/bin/${arch}-osdev-}
  local mlibc_cross_file="${BUILD_DIR}/mlibc_cross_file.${arch}"
  local options=(
    --cross-file ${mlibc_cross_file}
    --prefix=${install_dir}
    --buildtype=${build_type}
    --libdir=lib

    -Ddisable_crypt_option=true
    -Ddisable_iconv_option=true
    -Ddisable_intl_option=true
  )
  case "$component" in
    lib)
      # bootstrap-system-gcc
      build_dir=${MLIBC_BUILD_DIR:-${BUILD_DIR}/mlibc/build}
      options+=( -Dmlibc_no_headers=true )
      ;;
    headers)
      build_dir=${MLIBC_BUILD_DIR:-${BUILD_DIR}/mlibc/headers}
      options+=( -Dheaders_only=true --wrap-mode=nofallback )
      ;;
    *)
      toolchain::util::error "invalid build component: ${component}"
      exit 1
      ;;
  esac

  if [ ! -d ${mlibc_dir} ]; then
    toolchain::mlibc::setup
  fi

  export ARCH=${arch}
  export CROSS_CC=${cross_prefix}gcc
  export CROSS_CXX=${cross_prefix}g++
  export CROSS_AR=${cross_prefix}ar
  export CROSS_STRIP=${cross_prefix}strip
  export CROSS_PKG_CONFIG=pkg-config
  export PKG_CONFIG_LIBDIR=${install_dir}/lib/pkgconfig

  cat ${PROJECT_DIR}/scripts/mlibc-cross-file.txt | envsubst > ${mlibc_cross_file}

  mkdir -p ${sysroot}
  mkdir -p ${build_dir}
  pushd ${mlibc_dir}
    echo -e "-> ${CYAN}toolchain::util::configure_step meson ${options[@]} ${build_dir}${RESET}"
    toolchain::util::configure_step meson ${options[@]} ${build_dir}
  popd
  pushd ${build_dir}
    echo -e "->  ${CYAN}toolchain::util::build_step ninja${RESET}"
    toolchain::util::build_step ninja
    echo -e "-> ${CYAN}toolchain::util::install_step ninja install${RESET}"
    toolchain::util::install_step ninja install
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
    cxxshim)
      toolchain::cxxshim::install $@
      ;;
    frigg)
      toolchain::frigg::install $@
      ;;
    setup)
      toolchain::mlibc::setup $@
      ;;
    build)
      toolchain::mlibc::build $@
      ;;
    *)
      toolchain::util::error "unsupported command '${command}'"
      exit 1
      ;;
  esac
fi
