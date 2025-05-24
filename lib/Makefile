# Lib sources
$(call register,lib,KERNEL)

# lib/
lib += atomic.c bitmap.c hash_table.c \
	interval_tree.c murmur3.c rb_tree.c sort.c

# lib/asm
lib += asm/atomic.asm asm/bits.asm

# lib/fmt
# even though it is a submodule, we'll include it in the build tree directly
lib += fmt/fmt.c fmt/fmtlib.c
