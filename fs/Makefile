# Fs sources
$(call register,fs,KERNEL)

# fs/
fs += dcache.c dentry.c file.c fs.c inode.c path.c super.c

# fs/initrd
fs += initrd/initrd.c initrd/inode.c initrd/super.c

# drivers-y
fs-y = $(call target-objects,fs)
