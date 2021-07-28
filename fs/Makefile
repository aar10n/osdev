#
# Fs Makefile
#

# fs
fs += acl.c blkdev.c chrdev.c dcache.c dentry.c \
	device.c file.c framebuf.c fs.c inode.c path.c \
	super.c utils.c

# fs/devfs
fs += devfs/devfs.c

# fs/ext2
fs += ext2/ext2.c ext2/file.c ext2/inode.c ext2/super.c

# fs/fat
#fs += fat/fat.c fat/file.c fat/inode.c fat/super.c

# fs/ramfs
fs += ramfs/file.c ramfs/inode.c ramfs/ramfs.c ramfs/super.c

fs-y = $(call objects,fs,$(BUILD_DIR)/fs)
