#
# libc Makefile
#

INCLUDE-libc = -Ilibc -Iinclude/common

# libc
libc += libgen.c math.c stdio.c \
		stdlib.c string.c

# libc-y
libc-y = $(call objects,libc,$(BUILD_DIR)/libc)
