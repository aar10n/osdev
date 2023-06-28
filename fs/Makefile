# Fs sources
$(call register,fs,KERNEL)

# fs/initrd
fs += initrd/initrd.c initrd/initrd_vfsops.c initrd/initrd_vnops.c

# fs/ramfs
fs += ramfs/memfile.c ramfs/ramfs.c ramfs/ramfs_vfsops.c ramfs/ramfs_vnops.c

fs-y = $(call target-objects,fs)
