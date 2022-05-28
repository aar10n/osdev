#!/bin/bash

# Common functions used across toolchain scripts
#
# Environment variables:
#   PROJECT_DIR    - absolute path of the main project directory
#   BUILD_DIR      - absolute path of the build directory
#   SYS_ROOT       - system root for the os disk image
#
#   NPROC          - number of parallel jobs to use for make
#   SKIP_CONFIGURE - skip configure step if set
#   SKIP_BUILD     - skip build step if set
#   SKIP_INSTALL   - skip install step if set

BUILD_DIR=${BUILD_DIR:-${PROJECT_DIR}/build}
SYS_ROOT=${SYS_ROOT:-${BUILD_DIR}/sysroot}
NPROC=${NPROC:-$(nproc)}

# toolchain::util::check_args
# args:
#   <1>: number required arguments
#   <2...N>: arguments to check
toolchain::util::check_args() {
  local context="${FUNCNAME[1]}"
  local n="$1"
  shift

  if [[ ${FUNCNAME[1]} == "main" ]]; then
    context="${BASH_SOURCE[1]}"
  fi

  count=0
  for arg in "$@"; do
    if [[ ( ${count} -le ${n} ) && ( -z "${arg}" ) ]]; then
      echo "${context}: argument $((count)) is empty" >&2
      exit 1
    fi
    ((count++))
  done

  if [[ ${count} -lt ${n} ]]; then
    echo "${context}: expected ${n} argument(s), got $count" >&2
    exit 1
  fi
}

# toolchain::util::error
# args:
#   <1>: message
toolchain::util::error() {
  local context="${FUNCNAME[1]}"
  if [[ ${FUNCNAME[1]} == "main" ]]; then
    context="${BASH_SOURCE[1]}"
  fi
  echo "${context}: error: $1" >&2
}

# toolchain::util::configure
# args:
#   <1>: configure path
#   <2..N>: options
toolchain::util::configure() {
  local configure="$1"
  shift

  if [[ -n ${SKIP_CONFIGURE} ]]; then
    echo "skipping configure"
    return 0
  fi

  ${configure} $@ ${COMMON_OPTIONS}
}

# toolchain::util::make_build
# args:
#   <1..N>: arguments
toolchain::util::make_build() {
  if [[ -n ${SKIP_BUILD} ]]; then
    echo "skipping build"
    return 0
  fi

  make -j${NPROC} $@
}

# toolchain::util::make_install
# args:
#   <1>: target
toolchain::util::make_install() {
  if [[ -n ${SKIP_INSTALL} ]]; then
    echo "skipping install"
    return 0
  fi

  make ${1:-install}
}

