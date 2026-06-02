DES = build
TARGETELF = $(DES)/kernel.elf
TARGETBIN = $(DES)/kernel.bin

TESTDIR = test
IMG = $(TESTDIR)/disk.img
MOUNT = /mnt/diskimg
GRUBCONFIG = test/boot/grub.cfg
LIMINECONFIG = test/boot

LOOPDRIVE := $(shell sudo losetup -f)
LOOPDRIVEP1 := "p1"
LOOPDRIVEP2 := "p2"

createdisk:
	@echo ">> Creating 'disk.img' file ......"
	@mkdir -p $(dir IMG)
	dd if=/dev/zero of=$(IMG) bs=1M count=128
	
	@echo ">> Creating EFI System Partition — 20MB"
	@sgdisk \
		-n 1:2048:+40M \
		-t 1:C12A7328-F81F-11D2-BA4B-00A0C93EC93B \
		$(IMG)
	
	@echo ">> Creating Normal FAT32 partition — 50MB"
	@sgdisk \
		-n 2:0:+64M \
		-t 2:EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 \
		$(IMG)
	@echo ">> $(LOOPDRIVE)"
	@sudo losetup -fP $(IMG)
	@echo ">> Formating Fat32 to $(LOOPDRIVE)$(LOOPDRIVEP1)"
	@sudo mkfs.vfat -F 32 $(LOOPDRIVE)$(LOOPDRIVEP1)
	@echo ">> Formating Fat32 to $(LOOPDRIVE)$(LOOPDRIVEP2)"
	@sudo mkfs.vfat -F 32 $(LOOPDRIVE)$(LOOPDRIVEP2)
	@sudo losetup -d $(LOOPDRIVE)   # detach loop device
	@echo ">> Done"

writegrub:
	@echo ">> Writing bootloader......."
	@sudo mkdir -p $(MOUNT)
	@sudo losetup -fP $(IMG)
	@sudo mount $(LOOPDRIVE)$(LOOPDRIVEP1) $(MOUNT)
	@echo ">> creating dir $(MOUNT)/boot/grub2"
	@sudo mkdir -p $(MOUNT)/boot/grub2
	@echo ">> creating dir $(MOUNT)/EFI/BOOT"
	@sudo mkdir -p $(MOUNT)/EFI/BOOT
	@echo ">> coping file $(GRUBCONFIG) to $(MOUNT)/boot/grub2"
	@sudo cp $(GRUBCONFIG) $(MOUNT)/boot/grub2
	@sudo grub2-install \
		--target=x86_64-efi \
		--efi-directory=$(MOUNT)\
		--boot-directory=$(MOUNT)/boot \
		--removable \
		--recheck \
		--no-nvram \
		--force
	@sudo umount $(MOUNT)
	@sudo losetup -d $(LOOPDRIVE)   # detach loop device
	@echo ">> Done"


writelimine:
	@echo ">> Writing bootloader......."
	@sudo mkdir -p $(MOUNT)
	@sudo losetup -fP $(IMG)
	@sudo mount $(LOOPDRIVE)$(LOOPDRIVEP1) $(MOUNT)
	@echo ">> creating dir $(MOUNT)/boot/limine"
	@sudo mkdir -p $(MOUNT)/boot/limine
	@echo ">> creating dir $(MOUNT)/EFI/BOOT"
	@sudo mkdir -p $(MOUNT)/EFI/BOOT
	@echo ">> coping file $(LIMINECONFIG)/limine.conf to $(MOUNT)/boot/limine"
	@sudo cp $(LIMINECONFIG)/limine.conf $(MOUNT)/boot/limine
	@echo ">> coping file $(LIMINECONFIG)/BOOTX64.EFI to $(MOUNT)/EFI/BOOT/"
	@sudo cp  $(LIMINECONFIG)/BOOTX64.EFI $(MOUNT)/EFI/BOOT/
	@echo ">> coping file $(LIMINECONFIG)/BOOTIA32.EFI to $(MOUNT)/EFI/BOOT/"
	@sudo cp  $(LIMINECONFIG)/BOOTIA32.EFI $(MOUNT)/EFI/BOOT/
	@sudo umount $(MOUNT)
	@sudo losetup -d $(LOOPDRIVE)   # detach loop device
	@echo ">> Done"


loadkernel:
	@echo ">> Coping a kernel to disk....."
	@sudo mkdir -p $(MOUNT)
	@sudo losetup -fP $(IMG)
	@sudo mount $(LOOPDRIVE)$(LOOPDRIVEP2) $(MOUNT)
	@echo ">> creating dir $(MOUNT)/boot"
	@sudo mkdir -p $(MOUNT)/boot
	@echo ">> coping file $(TARGETELF) to $(MOUNT)/boot"
	@sudo cp $(TARGETELF) $(MOUNT)/boot
	@sudo umount $(MOUNT)
	@sudo losetup -d $(LOOPDRIVE)   # detach loop device
	@echo ">> Done"


run:
	@echo ">> Starting a emulator..."
	@sudo qemu-system-x86_64 \
		-cpu qemu64 \
		-m 512M \
		-d int,guest_errors,cpu_reset \
		-D test/qemu.log \
		-bios /usr/share/edk2/ovmf/OVMF_CODE.fd \
		-drive file=$(IMG),format=raw,if=ide \
		-net none \
		-serial stdio 
# 		-device VGA,xres=1920,yres=1200
