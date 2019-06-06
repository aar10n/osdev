BUILD = cmake-build-debug

run: $(BUILD)/osdev.iso $(BUILD)/disk.img
	qemu-system-i386 -serial file:$(BUILD)/stdio		\
		-drive file=$(BUILD)/disk.img,format=raw,if=ide \
		-drive file=$(BUILD)/osdev.iso,media=cdrom	    \

debug: $(BUILD)/osdev.iso $(BUILD)/disk.img
	qemu-system-i386 -s -S 								\
    	-drive file=$(BUILD)/disk.img,format=raw,if=ide \
        -drive file=$(BUILD)/osdev.iso,media=cdrom	    &
	i386-elf-gdb -w 									\
		-ex "target remote localhost:1234"				\
		-ex "add-symbol $(BUILD)/kernel"

# -------------- #
#  Dependencies  #
# -------------- #

# osdev.iso - Bootable ISO
$(BUILD)/osdev.iso: $(BUILD)/kernel grub.cfg
	mkdir -p $(BUILD)/iso/boot/grub
	cp $< $(BUILD)/iso/boot/osdev
	cp grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	i386-elf-grub-mkrescue -o $@ $(BUILD)/iso &> /dev/null

# kernel - Kernel binary
.PHONY: $(BUILD)/kernel
$(BUILD)/kernel:
	make -C $(BUILD) kernel

# disk.img - Virtual HDD
$(BUILD)/disk.img:
	dd if=/dev/zero of=$@ bs=1024 count=1024
