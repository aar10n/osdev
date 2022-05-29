#!/bin/bash
set -e

PROJECT_DIR=$(realpath `dirname ${BASH_SOURCE[0]}`/..)
source "${PROJECT_DIR}/toolchain/common.sh"

#----------------------------------

readonly EDK2_VERSION=edk2-stable202202

# copy bootloader sources into edk2 source tree
toolchain::edk2::copy_boot_sources() {
  local edk2_dir=${EDK2_DIR:-${BUILD_DIR}/edk2}
  local pkg_dir=${edk2_dir}/LoaderPkg
  if [[ ! -d "${edk2_dir}" ]]; then
    return 0
  fi

  mkdir -p ${pkg_dir}
  cp -t ${pkg_dir} ${PROJECT_DIR}/boot/{LoaderPkg.dsc,Loader.inf}
  cp -t ${pkg_dir} ${PROJECT_DIR}/boot/*.c
  cp -t ${pkg_dir} ${PROJECT_DIR}/include/boot/*.h
  cp -t ${pkg_dir} ${PROJECT_DIR}/include/{boot,elf,elf64}.h
}

# download and setup edk2
# =======================
# env:
#   EDK2_DIR - edk2 directory (default = <BUILD_DIR>/edk2)
#
# example:
#   toolchain::edk2::setup
toolchain::edk2::setup() {
  local edk2_dir=${EDK2_DIR:-${BUILD_DIR}/edk2}
  local build_dir=$(dirname ${edk2_dir})
  mkdir -p ${build_dir}

  export WORKSPACE="${edk2_dir}"
  if [ ! -d ${edk2_dir} ]; then
    git clone --depth 1 -c advice.detachedHead=false --branch ${EDK2_VERSION} https://github.com/tianocore/edk2.git ${edk2_dir}
    pushd ${edk2_dir}
      git submodule update --init
      patch -p1 -d . < ${PROJECT_DIR}/toolchain/patches/edk2.patch
    popd
  fi

  pushd ${edk2_dir}
    . edksetup.sh
    CXX=llvm toolchain::util::make_build -C BaseTools
  popd
}

# build edk2 packages
# ===================
# args:
#   <1>: target triplet
#   <2>: package [loader|loader-lst|ovmf]
# env:
#   EDK2_DIR        - edk2 directory (default = <BUILD_DIR>/edk2)
#   EDK2_BUILD_TYPE - build type [RELEASE|DEBUG] (default = RELEASE)
#
# example:
#   toolchain::edk2::build X64 loader
#   toolchain::edk2::build x86_64 ovmf
toolchain::edk2::build() {
  toolchain::util::check_args 2 $@

  local edk2_dir=${EDK2_DIR:-${BUILD_DIR}/edk2}
  local build_dir=$(dirname ${edk2_dir})
  mkdir -p ${build_dir}

  export WORKSPACE="${edk2_dir}"
  if [ ! -d ${edk2_dir} ]; then
    git clone --depth 1 -c advice.detachedHead=false --branch ${EDK2_VERSION} https://github.com/tianocore/edk2.git ${edk2_dir}
    pushd ${edk2_dir}
      git submodule update --init
      patch -p1 -d . < ${PROJECT_DIR}/toolchain/patches/edk2.patch
      . edksetup.sh
      CXX=llvm toolchain::util::make_build -C BaseTools
    popd
  fi

  local arch=""
  local package=""

  target="$1"
  case "${target}" in
    x86_64* | X64)
      arch="X64"
      ;;
    *)
      toolchain::util::error "unsupported target: $1"
      exit 1
      ;;
  esac

  package_name="$2"
  case "${package_name}" in
    ovmf)
      package="OvmfPkg/OvmfPkg${arch}.dsc"
      ;;
    loader)
      package="LoaderPkg/LoaderPkg.dsc"
      ;;
    loader-lst)
      package="LoaderPkg/LoaderPkg.dsc"
      ;;
    *)
      toolchain::util::error "unsupported package: ${package}"
      exit 1
      ;;
  esac
  shift 2

  if [[ ${package_name} =~ "loader(-lst)?" ]]; then
    toolchain::edk2::copy_boot_sources
  fi

  local build_type=${EDK2_BUILD_TYPE:-RELEASE}
  pushd ${edk2_dir}
    . edksetup.sh --reconfig
    build -p ${package} -a ${arch} -t CLANGPDB -b ${build_type}
  popd

  if [[ -n ${SKIP_INSTALL} ]]; then
    return 0
  fi

  # copy output files
  echo "copying output files..."
  if [[ ${package_name} == "loader" ]]; then
    local output_dir=${edk2_dir}/Build/Loader/${build_type}_CLANGPDB/${arch}
    cp ${output_dir}/Loader.efi ${BUILD_DIR}/boot.efi
    echo "copied output to ${BUILD_DIR}/boot.efi"
  elif [[ ${package_name} == "loader-lst" ]]; then
    local output_dir=${edk2_dir}/Build/Loader/${build_type}_CLANGPDB/${arch}/LoaderPkg/Loader/OUTPUT
    local lst_file=${output_dir}/static_library_files.lst
    local autogen_obj=${output_dir}/AutoGen.obj

    cp ${output_dir}/static_library_files.lst ${BUILD_DIR}/static_library_files.lst
    sed -i "1s#.*#${autogen_obj}#" ${BUILD_DIR}/static_library_files.lst
    echo "copied output to ${BUILD_DIR}/static_library_files.lst"
  elif [[ ${package_name} == "ovmf" ]]; then
    cp ${edk2_dir}/Build/Ovmf${arch}/${build_type}_CLANGPDB/FV/OVMF.fd ${BUILD_DIR}/OVMF_${arch}.fd
    echo "copied output to ${BUILD_DIR}/OVMF_${arch}.fd"
  fi
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
    setup)
      toolchain::edk2::setup
      ;;
    build)
      toolchain::edk2::build $@
      ;;
    *)
      toolchain::util::error "unsupported command '${command}'"
      exit 1
      ;;
  esac
fi
