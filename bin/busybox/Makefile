# busybox utils
NAME = busybox
GROUP = bin
VERSION = 1.36.1

# Include the external program build system
include ../../scripts/extern.mk

BUSYBOX_URL = https://github.com/mirror/busybox/archive/refs/tags/$(subst .,_,$(VERSION)).tar.gz
BUSYBOX_CONFIG = config
BUSYBOX_ARCHIVE = $(NAME)-$(VERSION).tar.gz

BUSYBOX_APPLETS = \
	ash awk base32 base64 basename bunzip2 bzcat bzip2 cat cksum cmp comm \
	cp crc32 cut date dd diff dirname echo egrep env expand expr factor \
	false fgrep find fold grep gunzip gzip head link ln ls lzcat lzma \
	md5sum mkdir mkfifo mknod mktemp mv nice nl nohup nproc od paste \
	printenv printf pwd readlink realpath rm rmdir sed seq sh sha1sum \
	sha256sum sha3sum sha512sum shred shuf sleep sort split stat stty \
	sum tac tail tar tee test timeout touch tr true truncate tsort uname \
	unexpand uniq unlink unlzma unxz unzip usleep uuencode wc which \
	xargs xz xzcat yes zcat


# Required: Download and extract sources
.PHONY: download
download:
	@if [ ! -f "$(OBJ_DIR)/$(BUSYBOX_ARCHIVE)" ]; then \
		echo "Downloading busybox $(BUSYBOX_VERSION)..."; \
		cd $(OBJ_DIR) && wget -O $(BUSYBOX_ARCHIVE) $(BUSYBOX_URL); \
	fi
	@if [ ! -d "$(SOURCE_PATH)" ]; then \
		echo "Extracting busybox..."; \
		cd $(OBJ_DIR) && tar -xf $(BUSYBOX_ARCHIVE); \
		mv busybox-$(subst .,_,$(VERSION)) $(SOURCE_PATH); \
	fi

# Optional: Configure busybox
.PHONY: configure-extra
configure-extra:
	cp $(BUSYBOX_CONFIG) $(SOURCE_PATH)/.config

# Required: Main build command
.PHONY: build-main
build-main:
	$(MAKE) -C $(SOURCE_PATH) CC="$(CC)" AR="$(AR)" STRIP="$(STRIP)"
	@if [ -f $(SOURCE_PATH)/busybox_unstripped ]; then \
		cp $(SOURCE_PATH)/busybox_unstripped $(OBJ_DIR)/busybox; \
	elif [ -f $(SOURCE_PATH)/busybox ]; then \
		cp $(SOURCE_PATH)/busybox $(OBJ_DIR)/busybox; \
	else \
		echo "Error: busybox binary not found"; exit 1; \
	fi

# Optional: Install main binary and create symlinks for individual applets
.PHONY: install-extra
install-extra:
	@echo "Creating busybox applet symlinks..."
	@for applet in $(BUSYBOX_APPLETS); do \
		echo "Creating symlink: $$applet -> /bin/busybox"; \
		cd $(INSTALL_BINDIR) && ln -sf /bin/busybox $$applet; \
	done

# Optional: Clean busybox
.PHONY: clean-extra
clean-extra:
	@echo "Cleaning busybox build..."
	$(MAKE) -C $(SOURCE_PATH) clean

#

# Convenience target for configuration
.PHONY: menuconfig
menuconfig: $(DOWNLOADED_TARGET)
	$(MAKE) -C $(SOURCE_PATH) menuconfig
	cp $(SOURCE_PATH)/.config $(BUSYBOX_CONFIG)
