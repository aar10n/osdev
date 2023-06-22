# Hobby Operating System

This is a modern 64-bit UEFI based UNIX-like hobby operating system. It has been an 
ongoing project for a while and I treat it like a sandbox to learn about operating
system design and other low-level concepts. It is not yet fully working (no userspace 
or shell), but I have been slowly working towards the goal of running doom in userspace.

Currently, it has a working bootloader, memory management, scheduler, usb support (hid, 
mass storage),  and a few other things. I read a lot of source code from other projects
while working on this so maybe this could be useful to someone else.

```
├── apps          userspace programs
├── boot          uefi edk2 bootloader
├── drivers       external device drivers
├── fs            filesystem types
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
│  ├── usb          usb and usb device drivers
│  └── vfs          virtual filesystem code
├── lib           data structure, algorithm and other useful libraries
├── sbin          system binaries
├── scripts       build related scripts and support files
├── third-party   third party dependencies
└── toolchain     toolchain build files and patches 
```
