#
# Lib Makefile
#

# lib
lib += atomic.c bitmap.c \
	hash_table.c interval_tree.c \
	lock.c rb_tree.c

# lib/asm

lib += asm/atomic.asm asm/bitmap.asm


# lib-y
lib-y = $(call objects,lib,$(BUILD_DIR)/lib)
