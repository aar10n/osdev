## Hobby Operating System

This is a modern 64-bit UEFI based UNIX-like hobby operating system. It has been a side-project
for the past two years, and it is an ongoing effort with much left to add. Many subsystems are 
more or less working, but they still need to be put together before this is considered functional.

```
├── boot          uefi bootloader
├── drivers       external device drivers
├── fs            core filesystem code
│  ├── devfs        device filesystem driver (/dev)
│  ├── ext2         read only ext2 filesystem driver
│  └── ramfs        generic in-memory filesystem driver 
├── include       header files
├── kernel        core kernel code
│  ├── bus          pci & pcie drivers
│  ├── cpu          cpu related code and assembly routines
│  ├── device       apic, ioapic and other related drivers
│  ├── gui          graphics code [not started]
│  ├── mm           memory management (physical and virtual)
│  └── usb          usb and usb device drivers
├── lib           datastructures and algorithms implementations
├── scripts       build scripts
├── sys           userspace
├── third-party   third party dependencies
└── tools         other useful tools that require compilation 
``` 

### Goals

**Short Term:**
- Implement a windowing system
- Implement a system console
- Port DOOM

**Long Term:**
- POSIX compatibility
- Add a full GUI
- Networking
- Proper SMP Support

### Custom Toolchain

The custom toolchain can be built using the `toolchain.make` makefile.
It cross-compiles binutils, gcc, mlibc and generates the system root
directory structure. Userspace applications must be compiled with the
custom-built gcc in order to be properly linked to the ported standard 
library.
