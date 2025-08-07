# Kernel sources
$(call register,kernel,KERNEL)

KERNEL_CFLAGS = $(CFLAGS) -mcmodel=large -mno-red-zone -fno-stack-protector \
				-fno-omit-frame-pointer -fno-builtin-memset \
				$(KERNEL_DEFINES)

KERNEL_LDFLAGS = $(LDFLAGS) -Tlinker.ld -nostdlib -z max-page-size=0x1000 -L$(BUILD_DIR)

KERNEL_INCLUDE = $(INCLUDE) -Ilib -I$(TOOL_ROOT)/include

KERNEL_DEFINES = $(DEFINES) -D__KERNEL__


# kernel/
kernel += \
	entry.asm exception.asm memory.asm sigtramp.asm smpboot.asm syscall.asm switch.asm \
	alarm.c chan.c clock.c cond.c console.c device.c errno.c exec.c fs_utils.c init.c \
	input.c ipi.c irq.c kevent.c kio.c loadelf.c lock.c main.c mutex.c panic.c params.c \
	percpu.c printf.c proc.c rwlock.c sched.c sem.c signal.c smpboot.c string.c syscall.c \
	sysinfo.c time.c tqueue.c

# kernel/acpi
kernel += acpi/acpi.c acpi/pm_timer.c

# kernel/bus
kernel += bus/pci.c bus/pci_tables.c

# kernel/cpu
kernel += cpu/cpu.asm cpu/io.asm cpu/cpu.c cpu/gdt.c cpu/idt.c cpu/tcb.c

# kernel/debug
kernel += debug/debug.c debug/dwarf.c

# kernel/hw
kernel += hw/apic.c hw/hpet.c hw/ioapic.c hw/rtc.c hw/pit.c

# kernel/gui
kernel += gui/screen.c

# kernel/mm
kernel += mm/file.c mm/heap.c mm/init.c mm/pgcache.c mm/pmalloc.c mm/pgtable.c mm/vmalloc.c

# kernel/tty
kernel += tty/tty.c tty/ttydisc.c tty/ttyqueue.c

# kernel/usb
kernel += usb/usb.c

# kernel/vfs
kernel += vfs/file.c vfs/fs.c vfs/path.c vfs/pipe.c vfs/vcache.c vfs/ventry.c \
	vfs/vfs.c vfs/vnode.c vfs/vresolve.c
