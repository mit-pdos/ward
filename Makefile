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
QEMUSMP    ?= 4
# RAM to simulate (in MB)
QEMUMEM    ?= 1024
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
	$(Q)diff $@.tmp $@ > /dev/null || mv $@.tmp $@
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
	        -Ithird_party/acpica/source/include -Ithird_party/musl/include -nostdinc -Ithird_party/musl/arch/x86_64 \
	        -Ithird_party/musl/arch/generic -Ithird_party/libunwind/include
CFLAGS   := $(COMFLAGS) -std=c99
CXXFLAGS := $(COMFLAGS) -std=c++14 -Wno-sign-compare -faligned-new -DEXCEPTIONS=1 -Wno-delete-non-virtual-dtor -nostdinc++ -ferror-limit=1000
ASFLAGS  := $(ASFLAGS) -Iinclude -I$(O)/include -m64 -MD -MP -DHW_$(HW) -include param.h
LDFLAGS  :=

ALL := $(O)/ward.efi
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


##
## Qemu
##
QEMUACCEL ?= -M accel=kvm:hvf:hax:whpx:tcg
QEMUNET := -net user,hostfwd=tcp::2323-:23,hostfwd=tcp::8080-:80 -net nic,model=e1000
QEMUSERIAL := $(if $(QEMUOUTPUT),-serial file:$(QEMUOUTPUT),-serial mon:stdio)
QEMUCOMMAND = $(QEMU) -cpu Skylake-Client,+spec-ctrl,+md-clear -nographic -device sga \
			  -smp $(QEMUSMP) -m size=$(QEMUMEM) $(QEMUACCEL) $(QEMUNET) $(QEMUSERIAL) \
		      $(QEMUEXTRA) $(QEMUKERNEL) -no-reboot #-d int,cpu_reset

# We play a Makefile trick here: variables like QEMUCOMMAND which are declared with '=' are only
# evaluated when they are used. Thus future assignments to QEMUAPPEND and QEMUKERNEL (including this
# one) will be expanded in QEMUCOMMAND even though they happen after the definition of that
# variable. This lets us use "$(eval ...)" in build rules to change their values.
QEMUKERNEL = -kernel $(O)/ward.efi -append "$(QEMUAPPEND)"

qemu: $(O)/ward.efi
	$(QEMUCOMMAND)
qemu-gdb: $(O)/ward.efi
	$(QEMUCOMMAND) -s -S
qemu-grub: $(O)/ward.img
	$(eval QEMUKERNEL = )
	$(QEMUCOMMAND) $<
qemu-test: $(O)/ward.efi
	$(eval QEMUAPPEND += %/bin/unittests.sh)
	timeout --foreground 15m $(QEMUCOMMAND) -device isa-debug-exit
qemu-efi: $(O)/ward.efi
	$(Q)mkdir -p $(O)/fat/EFI/BOOT
	$(Q)cp $(O)/ward.efi $(O)/fat/EFI/BOOT/bootx64.efi
	$(Q)cp OVMF_VARS-1024x768.fd $(O)/OVMF_VARS-1024x768.fd
	$(eval QEMUKERNEL = )
	$(QEMUCOMMAND) -nodefaults -drive if=pflash,format=raw,readonly,file=OVMF_CODE.fd \
		-drive if=pflash,format=raw,file=$(O)/OVMF_VARS-1024x768.fd \
		-drive format=raw,file=fat:rw:$(O)/fat -machine q35


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
$(O)/boot.fat: $(O)/ward.efi grub/grub.cfg grub/grub.efi $(O)/writeok
	@echo "  GEN    $@"
	$(Q)dd if=/dev/zero of=$@ bs=4096 count=66560 2> /dev/null
	$(Q)mkfs.fat -F 32 -s 1 -S 512 $@ > /dev/null
	$(Q)mmd -i $@ ::EFI
	$(Q)mmd -i $@ ::EFI/Boot
	$(Q)mcopy -i $@ $(O)/ward.efi ::EFI/Boot/bootx64.efi
	$(Q)mcopy -i $@ grub/grub.cfg ::grub.cfg
	$(Q)mcopy -i $@ $(O)/writeok ::writeok
$(O)/ward.img: $(O)/boot.fat $(O)/fs.part grub/boot.img grub/core.img
	@echo "  GEN    $@"
	$(Q)truncate -s "101M" $@
	$(Q)PARTED_GPT_APPLE=0 parted -s --align minimal $@ mklabel gpt \
		mkpart primary 32KiB 1MiB \
		mkpart primary 1MiB 70MiB set 1 legacy_boot on set 2 esp on \
		mkpart primary 70MiB 100MiB
	$(Q)dd if=$(O)/boot.fat of=$@ conv=sparse obs=512 seek=2048 2> /dev/null
	$(Q)dd if=$(O)/fs.part of=$@ conv=sparse obs=512 seek=143360 2> /dev/null
	$(Q)dd bs=440 count=1 conv=notrunc if=grub/boot.img of=$@ 2> /dev/null
	$(Q)dd bs=512 seek=64 conv=notrunc if=grub/core.img of=$@ 2> /dev/null
$(O)/ward.vhdx: $(O)/ward.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vhdx $< $@
$(O)/ward.vmdk: $(O)/ward.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vmdk $< $@
$(O)/ward.vdi: $(O)/ward.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vdi $< $@
disks: $(O)/ward.img $(O)/ward.vhdx $(O)/ward.vmdk $(O)/ward.vdi

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
		biosdisk normal search part_msdos part_gpt fat multiboot multiboot2 all_video gfxmenu gfxterm echo probe
	$(Q)$(PYTHON) -c "print('\x41')" | dd of=$@.tmp bs=1 seek=500 count=1 conv=notrunc 2> /dev/null
	$(Q)mv $@.tmp $@

$(O)/ward.efi: efi_wrap/Cargo.toml efi_wrap/Cargo.lock efi_wrap/src/main.rs $(O)/kernel-stripped.elf
	@echo "  GEN    $@"
	$(Q)cargo +nightly build -Z build-std --target x86_64-unknown-uefi --release --manifest-path $< \
		--target-dir $(dir $@) -Z unstable-options --out-dir $(O)

##
## General commands
##
.PRECIOUS: $(O)/%.o
.PHONY: clean qemu qemu-gdb qemu-grub qemu-test disks

clean:
	rm -fr $(O)

all: $(ALL)
