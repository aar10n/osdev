# Fs sources
$(call register,fs,KERNEL)

# fs/devfs
fs += devfs/devfs.c devfs/devfs_vfsops.c

# fs/initrd
fs += initrd/initrd.c initrd/initrd_vfsops.c initrd/initrd_vnops.c

# fs/procfs
fs += procfs/procfs.c procfs/procfs_vfsops.c procfs/procfs_vnops.c procfs/procfs_examples.c

# fs/ramfs
fs += ramfs/memfile.c ramfs/ramfs.c ramfs/ramfs_vfsops.c ramfs/ramfs_vnops.c

fs-y = $(call target-objects,fs)
