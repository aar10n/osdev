#
# Lib Makefile
#

# lib
lib += atomic.c bitmap.c format.c \
	hash_table.c interval_tree.c \
	lock.c murmur3.c queue.c rb_tree.c \
	ringbuf.c

# lib/asm

lib += asm/atomic.asm asm/bits.asm


# lib-y
lib-y = $(call objects,lib,$(BUILD_DIR)/lib)
