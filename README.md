## Hobby Operating System

This is a modern 64-bit UEFI based hobby operating system. It has been my side-project
for the last two years, but it is still an ongoing effort. Many subsystems are more or 
less working, but they still need to be put together before this is considered functional.

```
.
├── boot          custom uefi bootloader
├── drivers       external device drivers
├── fs            vfs and filesystem drivers (in-progress)
├── include       header files
├── kernel        core kernel code
│  ├── bus          pci & pcie drivers
│  ├── cpu          cpu related code and assembly routines
│  ├── device       apic, ioapic and other related drivers
│  ├── gui          future home of graphics code
│  ├── mm           memory management (physical and virtual)
│  └── usb          usb and usb device drivers
├── lib           useful datastructure implementations
├── libc          minimal C standard library for use in kernel
├── scripts       build related scripts
├── sys           userspace files will live here
├── third-party   third party tools (currently only edk2)
└── tools         other useful tools that require compilation 
``` 
