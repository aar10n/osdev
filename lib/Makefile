#
# Lib Makefile
#

# lib
lib += atomic.c bitmap.c format.c \
	hash_table.c interval_tree.c \
	list.c murmur3.c \
	rb_tree.c ringbuf.c

# lib/asm

lib += asm/atomic.asm asm/bits.asm


# lib-y
lib-y = $(call target-objects,lib)
