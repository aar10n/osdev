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

# freetype
freetype = freetype-2.10.0
freetype_url = https://download.savannah.gnu.org/releases/freetype/freetype-2.10.0.tar.gz
freetype_dir = $(BUILD)/$(freetype)

# harfbuzz
harfbuzz = harfbuzz
harfbuzz_src = third-party/harfbuzz
harfbuzz_dir = $(BUILD)/$(harfbuzz)


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
		./configure --host=x86_64-unknown-elf --prefix=$(SYS_ROOT)/usr \
			--with-png=no --with-bzip2=no --with-harfbuzz=no --with-fsspec=no --with-fsref=no \
			--with-quickdraw-toolbox=no --with-quickdraw-carbon=no --with-ats=no --with-zlib=no

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
