#
# Boot Makefile
#

# boot specific flags
CFLAGS-boot = $(CFLAGS) \
	-target x86_64-unknown-windows \
	-fshort-wchar -mno-red-zone

LDFLAGS-boot = $(LDFLAGS) \
	-target x86_64-unknown-windows \
	-Wl,-entry:efi_main -Wl,-subsystem:efi_application \
	-fuse-ld=lld-link

# boot
boot += efi.c data.c

# boot-y
boot-y = $(call objects,boot,$(BUILD_DIR))
