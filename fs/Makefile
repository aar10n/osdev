#
# Fs Makefile
#

# fs
fs += acl.c blkdev.c chrdev.c dcache.c dentry.c \
	device.c file.c fs.c inode.c path.c super.c \
	utils.c

# fs/fat
#fs += fat/fat.c

# fs/ramfs
fs += ramfs/file.c ramfs/inode.c ramfs/ramfs.c ramfs/super.c

fs-y = $(call objects,fs,$(BUILD_DIR)/fs)
