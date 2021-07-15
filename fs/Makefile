#
# Fs Makefile
#

# fs
fs += acl.c blkdev.c device.c dcache.c \
	file.c fs.c inode.c path.c \
	stat.c utils.c vfs.c

# fs/fat
fs += fat/fat.c

# fs/proc
fs += proc/proc.c

# fs/ramfs
fs += ramfs/ramfs.c

fs-y = $(call objects,fs,$(BUILD_DIR)/fs)
