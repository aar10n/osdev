# Hobby Operating System

This is a modern 64-bit UEFI based UNIX-like hobby operating system. It has been a side-project
for the past few years, and it is an ongoing effort with much left to add. Right now it is not
a fully working OS, however many subsystems are in a more-or-less working state and so this may 
serve as a useful reference to some.

```
├── apps          userspace programs
├── boot          uefi edk2 bootloader
├── drivers       external device drivers
├── fs            core filesystem code
│  ├── devfs        device filesystem driver
│  ├── ext2         ext2 filesystem driver (read only)
|  ├── fat          fat12/16/32 filesystem driver (not working yet)
│  └── ramfs        generic in-memory filesystem driver 
├── include       header files
├── kernel        core kernel code
│  ├── acpi         acpi drivers
│  ├── bus          pci & pcie drivers
│  ├── cpu          cpu related code and assembly routines
│  ├── debug        debugging facilities (DWARF, stacktrace, etc)
│  ├── device       apic, ioapic and other related drivers
│  ├── gui          graphics code
│  ├── mm           memory management (physical and virtual)
│  ├── sched        scheduler and scheduling algorithms
│  └── usb          usb and usb device drivers
├── lib           data structure, algorithm and other useful libraries
├── scripts       utility scripts and support files
├── third-party   third party dependencies
└── toolchain     toolchain build files and patches 
``` 
