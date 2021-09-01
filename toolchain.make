ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD = $(ROOT)/build
SYS_ROOT = $(BUILD)/sysroot
PREFIX = $(SYS_ROOT)

NPROC = 6

# -------------- #
#  Dependencies  #
# -------------- #

# binutils
binutils = binutils-2.37
binutils_url = https://ftp.gnu.org/gnu/binutils/$(binutils).tar.gz
binutils_dir = $(BUILD)/$(binutils)

# gcc
gcc = gcc-11.2.0
gcc_url = https://ftp.gnu.org/gnu/gcc/$(gcc)/$(gcc).tar.gz
gcc_dir = $(BUILD)/$(gcc)

# mlibc
mlibc = mlibc
mlibc_src = third-party/mlibc
mlibc_dir = $(BUILD)/$(mlibc)
#mlibc_src = https://github.com/managarm/mlibc/archive/refs/tags/2.0.0.tar.gz


get_name = $(firstword $(subst _, ,$(subst -, ,$(notdir $1))))


#
# binutils
#

.PHONY: binutils
binutils: binutils-config binutils-compile binutils-install

.PHONY: binutils-config
binutils-config: $(binutils_dir)/src $(binutils_dir)/out | $(SYS_ROOT)
	scripts/apply-patch.sh $< scripts/binutils.patch
	cd $(binutils_dir)/out && $</configure --srcdir=$< --target=x86_64-osdev \
		--prefix=$(PREFIX) --with-sysroot=$(SYS_ROOT) \
    	--disable-werror --enable-targets=x86_64-elf CFLAGS=-O2

.PHONY: binutils-compile
binutils-compile:
	$(MAKE) -C $(binutils_dir)/out -j$(NPROC)

.PHONY: binutils-install
binutils-install:
	$(MAKE) -C $(binutils_dir)/out install

#
# gcc
#

.PHONY: gcc
gcc: gcc-config gcc-compile gcc-install

.PHONY: gcc-config
gcc-config: $(gcc_dir)/src $(gcc_dir)/out
	scripts/apply-patch.sh $< scripts/gcc.patch
	cd $< && ./contrib/download_prerequisites
	cd $</gcc && autoconf
	cd $</libgcc && autoconf
	cd $</libstdc++-v3 && autoconf
	cd $(gcc_dir)/out && $</configure --srcdir=$< --target=x86_64-osdev \
		--prefix=$(PREFIX) --with-sysroot=$(SYS_ROOT) --enable-languages=c,c++ \
		--disable-multilib --enable-initfini-array CFLAGS=-O2 CXXFLAGS=-O2

.PHONY: gcc-compile
# binutils mlibc-headers
gcc-compile:
	@mkdir -p $(SYS_ROOT)/usr/include
	$(MAKE) -C $(gcc_dir)/out -j$(NPROC) all-gcc

.PHONY: gcc-install
gcc-install:
	$(MAKE) -C $(gcc_dir)/out install-gcc
	ln -sf $(PREFIX)/x86_64-osdev/bin/as $(PREFIX)/bin/as
	ln -sf $(PREFIX)/x86_64-osdev/bin/ld $(PREFIX)/bin/ld

#
# libgcc
#

.PHONY: libgcc
libgcc: libgcc-compile libgcc-install

.PHONY: libgcc-compile
libgcc-compile:
	$(MAKE) -C $(gcc_dir)/out -j$(NPROC) all-target-libgcc

.PHONY: libgcc-install
libgcc-install:
	$(MAKE) -C $(gcc_dir)/out install-target-libgcc

#
# libstdc++
#

.PHONY: libstdc++
libstdc++: libstdc++-compile libstdc++-install

.PHONY: libstdc++-compile
libstdc++-compile:
	$(MAKE) -C $(gcc_dir)/out -j$(NPROC) all-target-libstdc++-v3

.PHONY: libstdc++-install
libstdc++-install:
	$(MAKE) -C $(gcc_dir)/out install-target-libstdc++-v3

#
# mlibc
#

.PHONY: mlibc
mlibc: mlibc-config mlibc-compile mlibc-install

.PHONY: mlibc-config
mlibc-config: $(mlibc_dir)/out $(BUILD)/mlibc-cross-file.txt | $(SYS_ROOT)
	cd $(mlibc_src) && meson --cross-file $(BUILD)/mlibc-cross-file.txt --prefix=$(SYS_ROOT) \
    	--libdir=lib --buildtype=debug -Dmlibc_no_headers=true $<

.PHONY: mlibc-compile
mlibc-compile:
	cd $(mlibc_dir)/out && ninja

.PHONY: mlibc-install
mlibc-install:
	cd $(mlibc_dir)/out && ninja install

#
# mlibc headers
#

.PHONY: mlibc-headers
mlibc-headers: mlibc-headers-config mlibc-headers-install

.PHONY: mlibc-headers-config
mlibc-headers-config: $(mlibc_dir)/headers $(BUILD)/mlibc-cross-file.txt | $(SYS_ROOT)
	cd $(mlibc_src) && meson --cross-file $(BUILD)/mlibc-cross-file.txt --prefix=$(PREFIX) \
		-Dheaders_only=true $<

.PHONY: mlibc-headers-install
mlibc-headers-install:
	cd $(mlibc_dir)/headers && ninja && ninja install


#
#
#

$(BUILD)/mlibc-cross-file.txt: scripts/mlibc-cross-file.m4
	m4 -DPREFIX=$(SYS_ROOT)/bin $< > $@

$(SYS_ROOT):
	@mkdir -p $@
	@mkdir $@/usr

$(BUILD)/%/src: $(BUILD)/%.tar.gz
	@mkdir -p $@ && tar -xf $< -C $@ --strip-components 1

$(BUILD)/%/out:
	@mkdir -p $@

$(BUILD)/%/headers:
	@mkdir -p $@

.PRECIOUS:
$(BUILD)/%.tar.gz:
	@mkdir -p $(dir $*)
	curl $($(call get_name, $@)_url) > $*.tar.gz
