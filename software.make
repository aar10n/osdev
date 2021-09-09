#
# third-party software
#

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD = $(ROOT)/build
SYS_ROOT = $(BUILD)/sysroot
PREFIX =

NPROC = 6

# -------------- #
#  Dependencies  #
# -------------- #

# zlib
zlib = zlib-1.2.11
zlib_url = https://zlib.net/zlib-1.2.11.tar.gz
zlib_dir = $(BUILD)/$(zlib)

# libpng
libpng = libpng-1.6.37
libpng_url = https://netactuate.dl.sourceforge.net/project/libpng/libpng16/1.6.37/libpng-1.6.37.tar.gz
libpng_dir = $(BUILD)/$(libpng)

# freetype
freetype = freetype-2.10.0
freetype_url = https://download.savannah.gnu.org/releases/freetype/freetype-2.10.0.tar.gz
freetype_dir = $(BUILD)/$(freetype)

# harfbuzz
harfbuzz = harfbuzz
harfbuzz_src = third-party/harfbuzz
harfbuzz_dir = $(BUILD)/$(harfbuzz)

#
# zlib
#

.PHONY: zlib
zlib: zlib-config zlib-compile zlib-install

.PHONY: zlib-config
zlib-config: $(zlib_dir)/src | $(SYS_ROOT)
	cd $< && CHOST=x86_64 CC=$(SYS_ROOT)/bin/x86_64-osdev-gcc AR=$(SYS_ROOT)/bin/x86_64-osdev-ar \
	  	RANLIB=$(SYS_ROOT)/bin/x86_64-osdev-ranlib \
		./configure --prefix=$(SYS_ROOT)/usr

.PHONY: zlib-compile
zlib-compile:
	$(MAKE) -C $(zlib_dir)/src -j$(NPROC)

.PHONY: zlib-install
zlib-install:
	$(MAKE) -C $(zlib_dir)/src install

#
# libpng
#

.PHONY: libpng
libpng: libpng-config libpng-compile libpng-install

.PHONY: libpng-config
libpng-config: $(libpng_dir)/src | $(SYS_ROOT)
	cd $< && CC=$(SYS_ROOT)/bin/x86_64-osdev-gcc AR=$(SYS_ROOT)/bin/x86_64-osdev-ar \
		RANLIB=$(SYS_ROOT)/bin/x86_64-osdev-ranlib \
		./configure --host=x86_64 --with-sysroot=$(SYS_ROOT) --prefix=$(SYS_ROOT)/usr

.PHONY: libpng-compile
libpng-compile:
	$(MAKE) -C $(libpng_dir)/src -j$(NPROC)

.PHONY: libpng-install
libpng-install:
	$(MAKE) -C $(libpng_dir)/src install

#
# freetype
#

.PHONY: freetype
freetype: freetype-config freetype-compile freetype-install

.PHONY: freetype-config
freetype-config: $(freetype_dir)/src | $(SYS_ROOT)
#	scripts/apply-patch.sh $< scripts/freetype.patch
	cd $< && CC=$(SYS_ROOT)/bin/x86_64-osdev-gcc \
		RANLIB=$(SYS_ROOT)/bin/x86_64-osdev-ranlib \
		AR=$(SYS_ROOT)/bin/x86_64-osdev-ar CFLAGS="-std=c99" \
		PKG_CONFIG_PATH=$(SYS_ROOT)/usr/lib \
		./configure --host=x86_64-unknown-elf --prefix=$(SYS_ROOT)/usr \
			--with-png=yes --with-bzip2=no --with-harfbuzz=no --with-fsspec=no --with-fsref=no \
			--with-quickdraw-toolbox=no --with-quickdraw-carbon=no --with-ats=no --with-zlib=yes

.PHONY: freetype-compile
freetype-compile:
	DESTDIR=$(SYS_ROOT) $(MAKE) -C $(freetype_dir)/src -j$(NPROC)

.PHONY: freetype-install
freetype-install:
	$(MAKE) -C $(freetype_dir)/src install
#
#
#

get_name = $(firstword $(subst _, ,$(subst -, ,$(notdir $1))))

$(SYS_ROOT):
	@mkdir -p $@
	@mkdir $@/usr

$(BUILD)/%/src: $(BUILD)/%.tar.gz
	@mkdir -p $@
	tar -xf $< -C $@ --strip-components 1

$(BUILD)/%/out:
	@mkdir -p $@

.PRECIOUS:
$(BUILD)/%.tar.gz:
	@mkdir -p $(dir $*)
	curl $($(call get_name, $@)_url) > $@
