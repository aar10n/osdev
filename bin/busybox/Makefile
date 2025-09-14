NAME = busybox
GROUP = bin
VERSION = 1.36.1

include ../../scripts/extern.mk

BUSYBOX_URL = https://github.com/mirror/busybox/archive/refs/tags/$(subst .,_,$(VERSION)).tar.gz
BUSYBOX_CONFIG = config
BUSYBOX_ARCHIVE = $(NAME)-$(VERSION).tar.gz

BIN_APPLETS = \
	ash basename cat cp cut date dd dirname echo env expand expr false \
	grep link ln ls mkdir mkfifo mknod mktemp mv nice nl nohup od \
	pwd readlink rm rmdir sh sleep test touch tr true uname wc

SBIN_APPLETS =

USR_BIN_APPLETS = \
	ascii awk base32 base64 bc bzcat bunzip2 bzip2 cksum cmp comm crc32 dc \
	diff df ed egrep factor fallocate fgrep find fold gunzip gzip head \
	hexdump hexedit lzcat lzma less man md5sum microcom nproc paste patch \
	printenv printf realpath sed seq sha1sum sha256sum sha3sum sha512sum sh \
	shred shuf sort split stat strings stty sum tac tail tar tee time \
	timeout tree truncate tsort unexpand uniq unlink unlzma unxz unzip \
	usleep uuencode vi which xargs xz xzcat yes zcat

USR_SBIN_APPLETS =


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

# Optional: Install symlinks for individual applets
.PHONY: install-extra
install-extra:
	@echo "Installing busybox applet symlinks..."
	$(call install-applets,BIN_APPLETS)
	$(call install-applets,SBIN_APPLETS)
	$(call install-applets,USR_BIN_APPLETS)
	$(call install-applets,USR_SBIN_APPLETS)

# Optional: Clean busybox
.PHONY: clean-extra
clean-extra:
	@echo "Cleaning busybox build..."
	$(MAKE) -C $(SOURCE_PATH) clean

# Convenience target for configuration
.PHONY: menuconfig
menuconfig: $(DOWNLOADED_TARGET)
	$(MAKE) -C $(SOURCE_PATH) menuconfig
	cp $(SOURCE_PATH)/.config $(BUSYBOX_CONFIG)

#

# install_applets <group>
define install-applets
	dir="$(call lowercase,$(patsubst %_APPLETS,%,$(1)))"; \
	dir=$$(echo "$$dir" | sed 's/_/\//g'); \
	mkdir -p $(INSTALL_PREFIX)/$$dir; \
	for applet in $($(1)); do \
		echo "Installing applet: $$applet to $(INSTALL_PREFIX)/$$dir"; \
		cd $(INSTALL_PREFIX)/$$dir && ln -sf /bin/busybox $$applet; \
	done
endef
