#
# Fs Makefile
#

# fs
fs += acl.c device.c dirent.c \
	file.c fs.c inode.c path.c \
	stat.c vfs.c

# fs/fat
fs += fat/fat.c fat/fat12.c \
	fat/fat16.c fat/fat32.c

# fs/proc
fs += proc/proc.c

# fs/ramfs
fs += ramfs/ramfs.c

fs-y = $(call objects,fs,$(BUILD_DIR)/fs)
