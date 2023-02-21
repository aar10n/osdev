# Fs sources
$(call register,fs,KERNEL)

# fs/
fs += bdev.c cdev.c dcache.c file.c \
	fs.c inode.c path.c super.c

# drivers-y
fs-y = $(call target-objects,fs)
