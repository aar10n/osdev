# This makefile is for compiling the development toolchain
include ../.config

CROSS_PREFIX = $(TOOL_ROOT)/bin/$(TOOLCHAIN)-

BINUTILS_DIR = $(BUILD_DIR)/binutils/$(TOOLCHAIN)
GCC_DIR = $(BUILD_DIR)/gcc/$(TOOLCHAIN)
MUSL_DIR = $(BUILD_DIR)/musl/$(TOOLCHAIN)
PKGCONFIG_DIR = $(BUILD_DIR)/pkgconfig/build
LIBDWARF_DIR = $(BUILD_DIR)/libdwarf/build


.NOTPARALLEL:
all: binutils gcc musl libdwarf musl-shared

clean:
	rm -rf $(BINUTILS_DIR)
	rm -rf $(GCC_DIR)
	rm -rf $(MUSL_DIR)
	rm -rf $(PKGCONFIG_DIR)
	rm -rf $(LIBDWARF_DIR)
	rm -rf $(BUILD_DIR)/toolchain
	rm -f $(GCC_SRC_DIR)/.setup
	rm -f $(LIBDWARF_SRC_DIR)/.setup

binutils: $(BINUTILS_DIR)/.install
gcc: $(GCC_DIR)/.install
musl: $(MUSL_DIR)/.install
musl-headers: $(MUSL_DIR)/.install-headers
musl-shared: $(MUSL_DIR)/.install-shared
pkg-config: $(PKGCONFIG_DIR)/.install
libdwarf: $(LIBDWARF_DIR)/.install


#
# binutils
#

BINUTILS_VERSION = 2.38
BINUTILS_ARCHIVE = binutils-$(BINUTILS_VERSION).tar.gz
BINUTILS_SOURCE = http://ftp.gnu.org/gnu/binutils/$(BINUTILS_ARCHIVE)
BINUTILS_SRC_DIR = $(BUILD_DIR)/binutils/src
BINUTILS_PREFIX = $(TOOL_ROOT)

BINUTILS_CONFIG = \
	--target=$(TOOLCHAIN) \
	--prefix=$(BINUTILS_PREFIX) \
	--srcdir=$(BINUTILS_SRC_DIR) \
	--with-sysroot=$(TOOL_ROOT) \
	--disable-werror --disable-multilib

$(BINUTILS_DIR)/.configure: | $(BINUTILS_SRC_DIR)
	mkdir -p $(@D)
	cd $(@D) && $(BINUTILS_SRC_DIR)/configure $(BINUTILS_CONFIG)
	@touch $@

$(BINUTILS_DIR)/.build: $(BINUTILS_DIR)/.configure
	cd $(@D) && $(MAKE)
	@touch $@

$(BINUTILS_DIR)/.install: $(BINUTILS_DIR)/.build
	cd $(@D) && $(MAKE) install
	@touch $@

#
# gcc
#

GCC_VERSION = 12.1.0
GCC_ARCHIVE = gcc-$(GCC_VERSION).tar.gz
GCC_SOURCE = https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VERSION)/$(GCC_ARCHIVE)
GCC_SRC_DIR = $(BUILD_DIR)/gcc/src
GCC_PREFIX = $(TOOL_ROOT)

GCC_CONFIG = --enable-languages=c,c++ \
	--target=$(TOOLCHAIN) \
	--prefix=$(GCC_PREFIX) \
	--srcdir=$(GCC_SRC_DIR) \
	--with-sysroot=$(TOOL_ROOT) \
	--with-build-sysroot=$(TOOL_ROOT) \
	--disable-shared \
	--disable-bootstrap --disable-multilib \
	--disable-libmpx --disable-libmudflap \
	--disable-libsanitizer --disable-libstdcxx \
	--disable-libgomp --disable-libssp\
	--enable-tls --enable-initfini-array \
	--enable-libstdcxx-filesystem-ts \
	--enable-libstdcxx-time=rt \
	\
	AR_FOR_TARGET=$(CROSS_PREFIX)ar \
	AS_FOR_TARGET=$(CROSS_PREFIX)as \
	LD_FOR_TARGET=$(CROSS_PREFIX)ld \
	NM_FOR_TARGET=$(CROSS_PREFIX)nm \
	OBJCOPY_FOR_TARGET=$(CROSS_PREFIX)objcopy \
	OBJDUMP_FOR_TARGET=$(CROSS_PREFIX)objdump \
	RANLIB_FOR_TARGET=$(CROSS_PREFIX)ranlib \
	READELF_FOR_TARGET=$(CROSS_PREFIX)readelf \
	STRIP_FOR_TARGET=$(CROSS_PREFIX)strip

$(GCC_SRC_DIR)/.setup: | $(GCC_SRC_DIR)
	cd $(@D) && ./contrib/download_prerequisites
	cd $(@D) && autoconf
	@touch $@

$(GCC_DIR)/.configure: $(GCC_SRC_DIR)/.setup $(BINUTILS_DIR)/.install
	mkdir -p $(@D)
	cd $(@D) && $(GCC_SRC_DIR)/configure $(GCC_CONFIG)
	@touch $@

$(GCC_DIR)/.gcc-only: $(GCC_DIR)/.configure
	mkdir -p $(TOOL_ROOT)/usr/include
	cd $(@D) && $(MAKE) all-gcc
	@touch $@

$(GCC_DIR)/.libgcc: $(MUSL_DIR)/.install
	cd $(@D) && $(MAKE) enable_shared=no all-target-libgcc
	@touch $@

$(GCC_DIR)/.build: $(GCC_DIR)/.libgcc $(MUSL_DIR)/.install
	cd $(@D) && $(MAKE)
	@touch $@

$(GCC_DIR)/.install: $(GCC_DIR)/.build
	cd $(@D) && $(MAKE) install-gcc install-target-libgcc
	@touch $@

#
# musl
#

MUSL_SRC_DIR = $(PROJECT_DIR)/third-party/musl
MUSL_PREFIX = $(TOOL_ROOT)/usr

# We need to make sure that --disable-shared
# is set in order to bootstrap libgcc.a for the
# first time.
MUSL_CONFIG = \
	--target=$(TOOLCHAIN) \
	--prefix= \
	--srcdir=$(MUSL_SRC_DIR) \
	--enable-debug \
	--disable-shared \
	\
	CFLAGS="-g -O0" \
	CC="$(GCC_DIR)/gcc/xgcc -B $(GCC_DIR)/gcc" \
	LIBCC="$(GCC_DIR)/$(TOOLCHAIN)/libgcc/libgcc.a"

MUSL_VARS = \
	AR=$(CROSS_PREFIX)ar \
	RANLIB=$(CROSS_PREFIX)ranlib \
	LDSO_PATHNAME=/lib/ld-musl-$(ARCH).so.1

$(MUSL_DIR)/.configure: $(GCC_DIR)/.gcc-only | $(MUSL_SRC_DIR)
	mkdir -p $(@D)
	cd $(@D) && $(MUSL_SRC_DIR)/configure $(MUSL_CONFIG)
	@touch $@

$(MUSL_DIR)/.build: $(MUSL_DIR)/.configure
	cd $(@D) && $(MAKE) $(MUSL_VARS)
	@touch $@

$(MUSL_DIR)/.install: export DESTDIR = $(MUSL_PREFIX)
$(MUSL_DIR)/.install: $(MUSL_DIR)/.build
	cd $(@D) && $(MAKE) install $(MUSL_VARS)
    # make the important abi/bits headers available to
    # the kernel by linking them into the include path
	mkdir -p $(TOOL_ROOT)/include
	ln -sfn $(MUSL_PREFIX)/include/bits $(TOOL_ROOT)/include/bits
	ln -sf $(MUSL_PREFIX)/include/features.h $(TOOL_ROOT)/include/features.h
	ln -sf $(MUSL_PREFIX)/include/limits.h $(TOOL_ROOT)/include/limits.h
	@touch $@

.PHONY: $(MUSL_DIR)/.install-headers
$(MUSL_DIR)/.install-headers: export DESTDIR = $(MUSL_PREFIX)
$(MUSL_DIR)/.install-headers: $(MUSL_DIR)/.configure
	cd $(@D) && $(MAKE) install-headers
# We need to build MUSL again as a shared object
# in order to make sure we have -fPIC enabled for the
# final kernel.
MUSL_CONFIG_SHARED = \
	--target=$(TOOLCHAIN) \
	--prefix= \
	--srcdir=$(MUSL_SRC_DIR) \
	--enable-debug \
	--enable-shared \
	\
	CFLAGS="-g -O0" \
	CC="$(GCC_DIR)/gcc/xgcc -B $(GCC_DIR)/gcc" \
	LIBCC="$(GCC_DIR)/$(TOOLCHAIN)/libgcc/libgcc.a"

$(MUSL_DIR)/.configure-shared: $(GCC_DIR)/.install | $(MUSL_SRC_DIR)
	mkdir -p $(@D)
	cd $(@D) && $(MUSL_SRC_DIR)/configure $(MUSL_CONFIG_SHARED)
	cd $(@D) && $(MAKE) $(MUSL_VARS) clean
	@touch $@

$(MUSL_DIR)/.build-shared: $(MUSL_DIR)/.configure-shared
	cd $(@D) && $(MAKE) $(MUSL_VARS)
	@touch $@

$(MUSL_DIR)/.install-shared: export DESTDIR = $(MUSL_PREFIX)
$(MUSL_DIR)/.install-shared: $(MUSL_DIR)/.build-shared
	cd $(@D) && $(MAKE) install $(MUSL_VARS)
    # make the important abi/bits headers available to
    # the kernel by linking them into the include path
	mkdir -p $(TOOL_ROOT)/include
	ln -sfn $(MUSL_PREFIX)/include/bits $(TOOL_ROOT)/include/bits
	ln -sf $(MUSL_PREFIX)/include/features.h $(TOOL_ROOT)/include/features.h
	ln -sf $(MUSL_PREFIX)/include/limits.h $(TOOL_ROOT)/include/limits.h
	@touch $@

#
# pkg-config
#

PKGCONFIG_VERSION = 0.29.2
PKGCONFIG_ARCHIVE = pkg-config-$(PKGCONFIG_VERSION).tar.gz
PKGCONFIG_SOURCE = https://pkgconfig.freedesktop.org/releases/$(PKGCONFIG_ARCHIVE)
PKGCONFIG_SRC_DIR = $(BUILD_DIR)/pkgconfig/src
PKGCONFIG_PREFIX = $(TOOL_ROOT)

PKGCONFIG_CONFIG = \
	--prefix=$(PKGCONFIG_PREFIX) \
	--with-sysroot=$(TOOL_ROOT) \
	--with-internal-glib

$(PKGCONFIG_DIR)/.configure: | $(PKGCONFIG_SRC_DIR)
	mkdir -p $(@D)
	cd $(@D) && $(PKGCONFIG_SRC_DIR)/configure $(PKGCONFIG_CONFIG)
	@touch $@

$(PKGCONFIG_DIR)/.build: $(PKGCONFIG_DIR)/.configure
	cd $(@D) && $(MAKE)
	@touch $@

$(PKGCONFIG_DIR)/.install: $(PKGCONFIG_DIR)/.build
	cd $(@D) && $(MAKE) install
	@touch $@

#
# libdwarf
#

LIBDWARF_VERSION = 0.5.0
LIBDWARF_ARCHIVE = libdwarf-$(LIBDWARF_VERSION).tar.xz
LIBDWARF_SOURCE = https://github.com/davea42/libdwarf-code/releases/download/v$(LIBDWARF_VERSION)/$(LIBDWARF_ARCHIVE)
LIBDWARF_SRC_DIR = $(BUILD_DIR)/libdwarf/src
LIBDWARF_PREFIX = $(TOOL_ROOT)

LIBDWARF_CONFIG = \
	--host=$(TOOLCHAIN) \
	--prefix=$(LIBDWARF_PREFIX) \
	--with-sysroot=$(TOOL_ROOT) \
	--with-pic \
	--disable-libelf \
	--disable-zlib \
	\
	CHOST=$(ARCH) \
	CFLAGS="-fPIC" \
	CC=$(CROSS_PREFIX)gcc \
	AR=$(CROSS_PREFIX)ar \
	RANLIB=$(CROSS_PREFIX)ranlib \
	PKG_CONFIG=

$(LIBDWARF_SRC_DIR)/.setup: | $(LIBDWARF_SRC_DIR)
	cd $(@D) && patch -p 1 -d . < $(PROJECT_DIR)/toolchain/patches/libdwarf.patch
	@touch $@

$(LIBDWARF_DIR)/.configure: $(LIBDWARF_SRC_DIR)/.setup
	mkdir -p $(@D)
	cd $(@D) && $(LIBDWARF_SRC_DIR)/configure $(LIBDWARF_CONFIG)
	@touch $@

$(LIBDWARF_DIR)/.build: $(LIBDWARF_DIR)/.configure
	cd $(@D) && $(MAKE)
	@touch $@

$(LIBDWARF_DIR)/.install: $(LIBDWARF_DIR)/.build
	cd $(@D) && $(MAKE) install
	@touch $@

#
# Support rules for fetching sources
#

uppercase = $(shell echo '$1' | tr 'a-z' 'A-Z')

# common target variables
$(BUILD_DIR)/%: name = $(firstword $(subst /, ,$(basename $*)))
$(BUILD_DIR)/%: varname = $(call uppercase,$(name))
$(BUILD_DIR)/%: source = $($(varname)_SOURCE)
$(BUILD_DIR)/%: archive = $($(varname)_ARCHIVE)
$(BUILD_DIR)/%: version = $($(varname)_VERSION)
$(BUILD_DIR)/%: filename = $(name)_$(subst .,_,$(version))

# submodule sources
$(PROJECT_DIR)/third-party/%:
	cd $(PROJECT_DIR) && git submodule update --init --recursive

# unpacking sources
.SECONDEXPANSION:
$(BUILD_DIR)/%/src: $(BUILD_DIR)/%/$$(archive)
	@mkdir -p $@
	tar -xf $< -C $@ --strip-components=1

# downloading sources
.PRECIOUS: $(BUILD_DIR)/%.tar.gz
$(BUILD_DIR)/%.tar.gz:
	@mkdir -p $(@D)
	wget -nc $(source) -o - -O $@

.PRECIOUS: $(BUILD_DIR)/%.tar.xz
$(BUILD_DIR)/%.tar.xz:
	@mkdir -p $(@D)
	wget -nc $(source) -o - -O $@
