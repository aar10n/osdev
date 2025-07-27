# Hobby Operating System

This is a modern 64-bit UEFI-based UNIX-like hobby operating system. It has been an 
ongoing project for a while, and it serves as a sandbox to learn about operating
system design and other low-level concepts. It is not fully functional, but it does 
currently support a simple shell, and the ability to run other programs, although
most will not work due to missing system call implementations. I have been slowly
working towards getting DOOM running in userspace.

```
├── bin           userspace programs
├── boot          uefi edk2 bootloader
├── drivers       external device drivers
│  └── usb          usb host/device drivers
├── fs            filesystem types
│  ├── devfs        device filesystem driver
│  ├── initrd       initial ramdisk filesystem driver
│  └── ramfs        generic in-memory filesystem driver 
├── include       header files
├── kernel        core kernel code
│  ├── acpi         acpi drivers
│  ├── bus          pci & pcie drivers
│  ├── cpu          cpu related code and assembly routines
│  ├── debug        debugging facilities (DWARF, stacktrace, etc)
│  ├── gui          graphics code (not used yet)
│  ├── hw           core system hardware drivers (apic, ioapic, etc)
│  ├── mm           physical and virtual memory management
│  ├── tty          terminal interface
│  ├── usb          usb interface
│  └── vfs          virtual filesystem code
├── lib           kernel data structures, algorithms and other misc. libraries
├── sbin          userspace system programs
├── scripts       build related scripts and support files
├── third-party   third party submodules
├── toolchain     toolchain build files and patches
└── tools         host development tools/plugins
```

# Toolchain

To build the full toolchain from source:
```shell
make -C toolchain all -j$(nproc)
```

# Building & Running

To set up a fresh environment and create the various required config files, run:
```shell
make setup
```
To compile the kernel and build a bootable system image, run:
```shell
make all
```
To run the system in QEMU, run:
```shell
make run
```
This will start QEMU, but it will not launch until a connection is opened on the
TCP server associated with the kernel console. You can do this by running:
```shell
telnet 127.0.0.1 8008
```

## QEMU Profiling Plugin

This project includes a custom QEMU plugin for profiling guest execution. You can find more information
about it in the [plugin README](tools/qemu-profile-plugin/README.md). This plugin will only be built if
the `QEMU_BUILD_PLUGIN` option is set to `y` in `.config`. When enabled, two additional Make targets are
available: `run-profile` and `profile-resolve`.
