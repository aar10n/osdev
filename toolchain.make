ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD = $(ROOT)/build
SYS_ROOT = $(BUILD)/sysroot
PREFIX = $(SYS_ROOT)

# -------------- #
#  Dependencies  #
# -------------- #


# binutils
binutils = binutils-2.37
binutils_src = https://ftp.gnu.org/gnu/binutils/$(binutils).tar.gz

# gcc
gcc = gcc-11.2.0
gcc_src = https://ftp.gnu.org/gnu/gcc/$(gcc)/$(gcc).tar.gz

#mlibc
mlibc = mlibc
mlibc_src = third-party/mlibc
#mlibc_src = https://github.com/managarm/mlibc/archive/refs/tags/2.0.0.tar.gz


get_name = $(firstword $(subst _, ,$(subst -, ,$(notdir $1))))


.PHONY: binutils
binutils: $(BUILD)/$(binutils) $(SYS_ROOT)
	-(cd $< && patch -p1 -N -r - < ../../scripts/binutils.patch)
	cd $< && ./configure --prefix=$(PREFIX) --target=x86_64-osdev --with-sysroot=$(SYS_ROOT) \
		--disable-werror --enable-targets=x86_64-elf,x86_64-pe CFLAGS=-O2
	$(MAKE) -C $< -j$(nproc)
	$(MAKE) -C $< install

.PHONY: gcc
gcc: $(BUILD)/$(gcc) $(SYS_ROOT)
	-(cd $< && patch -p1 -N -r - < ../../scripts/gcc.patch)
	cd $< && ./contrib/download_prerequisites
	cd $</gcc && autoconf
	cd $</libstdc++-v3 && autoconf
	cd $< && ./configure --prefix=$(PREFIX) --target=x86_64-osdev --with-sysroot=$(SYS_ROOT) \
		--enable-languages=c,c++ --disable-multilib --enable-initfini-array CFLAGS=-O2 CXXFLAGS=-O2
	$(MAKE) -C $< -j$(nproc) all-gcc
	$(MAKE) -C $< install-gcc


.PHONY: mlibc-headers
mlibc-headers: $(BUILD)/mlibc-headers
	cd $< && ninja && ninja install

.PHONY: mlibc/
mlibc: $(BUILD)/$(mlibc)
	cd $< && ninja && ninja install


# --------------------- #
#  General Build Rules  #
# --------------------- #

$(BUILD)/mlibc-headers: $(mlibc_src) $(SYS_ROOT) $(BUILD)/mlibc-cross-file.txt
	cd $< && meson --cross-file $(BUILD)/mlibc-cross-file.txt --prefix=$(PREFIX)/usr \
		-Dheaders_only=true $@

$(BUILD)/mlibc: $(mlibc_src) $(SYS_ROOT) $(BUILD)/mlibc-cross-file.txt
	cd $< && meson --cross-file $(BUILD)/mlibc-cross-file.txt --prefix=$(SYS_ROOT)/usr \
    		--libdir=lib --buildtype=debugoptimized -Dmlibc_no_headers=true $@


$(BUILD)/mlibc-cross-file.txt: scripts/mlibc-cross-file.m4
	m4 $< -DPREFIX=$(SYS_ROOT)/bin > $@

$(SYS_ROOT):
	@mkdir -p $@
	@mkdir $@/usr


$(BUILD)/%: $(BUILD)/%.tar.gz
	tar -xzf $< -C $(dir $@)

.PRECIOUS:
$(BUILD)/%.tar.gz:
	curl $($(call get_name, $@)_src) > $(dir $@)
