# Fs sources
$(call register,fs,KERNEL)

# fs/
fs += bdev.c cdev.c dcache.c dentry.c device.c \
	file.c fs.c inode.c path.c super.c

## fs/devfs
#fs += devfs/devfs.c
#
## fs/ext2
#fs += ext2/ext2.c ext2/file.c ext2/inode.c ext2/super.c
#
## fs/fat
##fs += fat/fat.c fat/file.c fat/inode.c fat/super.c
#
## fs/initrd
#fs += initrd/initrd.c initrd/file.c initrd/inode.c initrd/super.c
#
## fs/ramfs
#fs += ramfs/file.c ramfs/inode.c ramfs/ramfs.c ramfs/super.c

# drivers-y
fs-y = $(call target-objects,fs)
