## Hobby Operating System

This is a modern 64-bit UEFI based hobby operating system. It has been my side-project
for the last two years, but it is still an ongoing effort. Many subsystems are more or 
less working, but they still need to be put together before this is considered functional.

```
.
├── boot          uefi bootloader
├── drivers       external device drivers
├── fs            filesystem and filesystem drivers
├── include       header files
├── kernel        core kernel code
│  ├── bus          pci & pcie drivers
│  ├── cpu          cpu related code and assembly routines
│  ├── device       apic, ioapic and other related drivers
│  ├── gui          graphics code [not started]
│  ├── mm           memory management (physical and virtual)
│  └── usb          usb and usb device drivers
├── lib           datastructure implementations
├── libc          userspace C standard library
├── scripts       build scripts
├── sys           userspace
├── third-party   third party tools (currently only edk2)
└── tools         other useful tools that require compilation 
``` 
