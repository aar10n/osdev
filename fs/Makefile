# Fs sources
$(call register,fs,KERNEL)

# fs/
fs += dcache.c dentry.c file.c fs.c inode.c path.c super.c

# fs/ramfs
fs += ramfs/ramfs.c ramfs/file.c ramfs/inode.c ramfs/super.c

# drivers-y
fs-y = $(call target-objects,fs)
