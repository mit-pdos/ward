# Custom config file?  Otherwise use defaults.
-include config.mk
# Quiet.  Run "make Q=" for a verbose build.
Q          ?= @
# Prefix to use for ELF build tools, if the native toolchain isn't
# ELF.  E.g., x86_64-jos-elf-
TOOLPREFIX ?=
# QEMU binary
QEMU       ?= qemu-system-x86_64
# Number of CPUs to emulate
QEMUSMP    ?= 8
# RAM to simulate (in MB)
QEMUMEM    ?= 512
# Default hardware build target.  See param.h for others.
HW         ?= default
# Enable C++ exception handling in the kernel.
EXCEPTIONS ?= y
# Python binary
PYTHON     ?= python3
# Output directory
O           = output


#
# Tool definitions
#
CC  = $(TOOLPREFIX)clang
CXX = $(TOOLPREFIX)clang++
AR = $(TOOLPREFIX)ar
LD = $(TOOLPREFIX)ld
NM = $(TOOLPREFIX)nm
OBJDUMP = $(TOOLPREFIX)objdump
OBJCOPY = $(TOOLPREFIX)objcopy
STRIP = $(TOOLPREFIX)strip

define SYSCALLGEN
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(PYTHON) tools/syscalls.py $(1) kernel/*.cc > $@.tmp
	$(Q)cmp -s $@.tmp $@ || mv $@.tmp $@
endef


#
# Compiler flags
#
COMFLAGS := $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector) \
			$(shell $(CC) -fcf-protection=none -E -x c /dev/null >/dev/null 2>&1 && echo -fcf-protection=none) \
	        -static -fno-builtin -fno-strict-aliasing -fno-omit-frame-pointer -mcmodel=kernel -mno-sse \
	        -fms-extensions -mno-red-zone -nostdlib -ffreestanding -fno-pie -fno-pic -funwind-tables \
	        -fasynchronous-unwind-tables -g -MD -MP -O3 -Wall -msoft-float -mretpoline-external-thunk \
	        -DXV6_HW=$(HW) -DHW_$(HW) -DXV6 -DXV6_KERNEL \
	        -isystem include -iquote $(O)/include -include param.h -include include/compiler.h \
	        -Ithird_party/lwip/src/include -Inet -Ithird_party/lwip/src/include/ipv4 -Ithird_party/libcxx/include \
			-Ithird_party/acpica/source/include
CFLAGS   := $(COMFLAGS) -std=c99
CXXFLAGS := $(COMFLAGS) -std=c++14 -Wno-sign-compare -faligned-new -DEXCEPTIONS=1 -Wno-delete-non-virtual-dtor -nostdinc++ -ferror-limit=1000
ASFLAGS  := $(ASFLAGS) -Iinclude -I$(O)/include -m64 -MD -MP -DHW_$(HW) -include param.h
LDFLAGS  :=

ALL :=
all:


#
# Include other makefiles
#
include net/Makefrag
include third_party/Makefrag
include kernel/Makefrag
include bin/Makefrag
include tools/Makefrag
include metis/Makefrag


##
## generic build rules
##
$(O)/%.o: %.c
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(O)/%.o: %.cc
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: $(O)/%.cc
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: %.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

$(O)/bootx64.efi: $(KERN)
	objcopy --set-section-alignment *=4096 -j .text -j .rodata -j .stapsdt.base -j .kmeta -j .data -j .bss \
		-O pei-x86-64 $< $@


##
## Qemu
##
ifeq ($(QEMUSMP),1)
QEMUNUMA := -numa node
else
QEMUNUMA := -numa node -numa node
endif

QEMUACCEL ?= -M accel=kvm:hvf:tcg
QEMUAPPEND += root_disk=ahci0.0
QEMUNET := -net user,hostfwd=tcp::2323-:23,hostfwd=tcp::8080-:80 -net nic,model=e1000
QEMUSERIAL := $(if $(QEMUOUTPUT),-serial file:$(QEMUOUTPUT),-serial mon:stdio)
QEMUDISK := -drive if=none,file=$(O)/fs.part,format=raw,id=drive-sata0 -device ahci,id=ahci0 \
		   	-device ide-hd,bus=ahci0.0,drive=drive-sata0,id=sata0
QEMUCOMMAND = $(QEMU) -cpu Haswell,+pcid,+fsgsbase,+md-clear,+spec-ctrl -nographic -device sga \
		  	  -smp $(QEMUSMP) -m $(QEMUMEM) $(QEMUACCEL) $(QEMUNUMA) $(QEMUNET) $(QEMUSERIAL) \
		      $(QEMUDISK) $(QEMUEXTRA) $(QEMUKERNEL) -no-reboot

# We play a Makefile trick here: variables like QEMUCOMMAND which are declared with '=' are only
# evaluated when they are used. Thus future assignments to QEMUAPPEND and QEMUKERNEL (including this
# one) will be expanded in QEMUCOMMAND even though they happen after the definition of that
# variable. This lets us use "$(eval ...)" in build rules to change their values.
QEMUKERNEL = -kernel $(KERN) -append "$(QEMUAPPEND)"

qemu: $(KERN)
	$(QEMUCOMMAND)
qemu-gdb: $(KERN)
	$(QEMUCOMMAND) -s -S
qemu-grub: $(O)/ward.img
	$(eval QEMUKERNEL = )
	$(QEMUCOMMAND) $<
qemu-test: $(KERN)
	$(eval QEMUAPPEND += %/bin/unittests.sh)
	timeout --foreground 15m $(QEMUCOMMAND)

codex: $(KERN)


##
## Disk images
##
$(O)/writeok:
	$(Q)echo >$@
$(O)/fs.part: $(O)/tools/mkfs $(FSCONTENTS)
	@echo "  GEN    $@"
	$(Q)$(O)/tools/mkfs $@.tmp $(O)/fs
	$(Q)mv $@.tmp $@
$(O)/fs.part.gz: $(O)/fs.part
	@echo "  GEN    $@"
	$(Q)cat $^ | gzip -f -k -S ".gz.tmp" - > $@.tmp
	$(Q)mv $@.tmp $@
$(O)/boot.fat: $(KERN) grub/grub.cfg grub/grub.efi $(O)/writeok
	@echo "  GEN    $@"
	$(Q)dd if=/dev/zero of=$@ bs=4096 count=66560 2> /dev/null
	$(Q)mkfs.fat -F 32 -s 8 -S 512 $@ > /dev/null
	$(Q)mmd -i $@ ::EFI
	$(Q)mmd -i $@ ::EFI/BOOT
	$(Q)mcopy -i $@ grub/grub.efi ::EFI/BOOT/BOOTX64.EFI
	$(Q)mcopy -i $@ grub/grub.cfg ::grub.cfg
	$(Q)mcopy -i $@ $(KERN) ::ward
	$(Q)mcopy -i $@ $(O)/writeok ::writeok
$(O)/ward.img: $(O)/boot.fat $(O)/fs.part grub/boot.img grub/core.img
	@echo "  GEN    $@"
	$(Q)truncate -s "101M" $@
	$(Q)PARTED_GPT_APPLE=0 parted -s --align minimal $@ mklabel gpt \
		mkpart primary 32KiB 1MiB \
		mkpart primary 1MiB 70MiB set 1 legacy_boot on set 1 esp on \
		mkpart primary 70MiB 100MiB
	$(Q)dd if=$(O)/boot.fat of=$@ conv=sparse obs=512 seek=2048 2> /dev/null
	$(Q)dd if=$(O)/fs.part of=$@ conv=sparse obs=512 seek=143360 2> /dev/null
	$(Q)dd bs=440 count=1 conv=notrunc if=grub/boot.img of=$@ 2> /dev/null
	$(Q)dd bs=512 seek=64 conv=notrunc if=grub/core.img of=$@ 2> /dev/null
$(O)/ward.vhdx: $(O)/ward.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vhdx $< $@
$(O)/ward.vdi: $(O)/ward.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vdi $< $@

img: $(O)/ward.img
vhdx: $(O)/ward.vhdx
vdi: $(O)/ward.vdi

grub/boot.img:
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)cp /usr/lib/grub/i386-pc/boot.img $@.tmp
	$(Q)$(PYTHON) -c "print('\x40')" | dd of=$@.tmp bs=1 seek=92 count=1 conv=notrunc 2> /dev/null
	$(Q)mv $@.tmp $@
grub/core.img: grub/grub-early.cfg
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)grub-mkimage -O i386-pc -o $@.tmp -p '/' -c grub/grub-early.cfg \
		biosdisk normal search part_msdos part_gpt fat multiboot multiboot2 gfxmenu echo probe
	$(Q)$(PYTHON) -c "print('\x41')" | dd of=$@.tmp bs=1 seek=500 count=1 conv=notrunc 2> /dev/null
	$(Q)mv $@.tmp $@
grub/grub.efi: grub/grub-early.cfg
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)grub-mkimage -O x86_64-efi -o $@ -p '/' -c grub/grub-early.cfg \
		normal search part_msdos part_gpt fat multiboot multiboot2 gfxmenu echo video probe


##
## General commands
##
.PRECIOUS: $(O)/%.o
.PHONY: clean qemu qemu-gdb qemu-grub qemu-test

clean:
	rm -fr $(O)

all: $(ALL)
