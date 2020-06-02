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
HW         ?= qemu
# Enable C++ exception handling in the kernel.
EXCEPTIONS ?= y
# Shell command to run in VM after booting
RUN        ?= $(empty)
# Python binary
PYTHON     ?= python3
# Directory containing mtrace-magic.h for HW=mtrace
MTRACESRC  ?= ../mtrace
# Mtrace-enabled QEMU binary
MTRACE     ?= $(MTRACESRC)/x86_64-softmmu/qemu-system-x86_64

O           = o.$(HW)

ifeq ($(HW),linux)
PLATFORM   := native
TOOLPREFIX :=
else
PLATFORM   := xv6
endif

ifeq ($(HW),codex)
CODEXINC = -Icodexinc
else
CODEXINC =
endif

CC  = $(TOOLPREFIX)clang
CXX = $(TOOLPREFIX)clang++
CFLAGS   =
ASFLAGS  =

AR = $(TOOLPREFIX)ar
LD = $(TOOLPREFIX)ld
NM = $(TOOLPREFIX)nm
OBJDUMP = $(TOOLPREFIX)objdump
OBJCOPY = $(TOOLPREFIX)objcopy
STRIP = $(TOOLPREFIX)strip

ifeq ($(PLATFORM),xv6)
INCLUDES  = -nostdinc++ -isystem include -iquote $(O)/include -iquote libutil/include \
		 -include param.h -include libutil/include/compiler.h
COMFLAGS  = -static -DXV6_HW=$(HW) -DXV6 \
	    -fno-builtin -fno-strict-aliasing -fno-omit-frame-pointer -fms-extensions \
	    -mno-red-zone -nostdlib -ffreestanding -fno-pie -fno-pic -DNDEBUG -funwind-tables -fasynchronous-unwind-tables
LDFLAGS   = -m elf_x86_64 --eh-frame-hdr
else
INCLUDES := -include param.h -iquote libutil/include -I$(MTRACESRC)
COMFLAGS := -pthread -Wno-unused-result
LDFLAGS := -pthread
endif

# -mindirect-branch=thunk -fnothrow-opt -Wnoexcept -Wl,-m,elf_x86_64

COMFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector) \
			$(shell $(CC) -fcf-protection=none -E -x c /dev/null >/dev/null 2>&1 && echo -fcf-protection=none) \
			-g -MD -MP -O3 -Wall -DHW_$(HW) $(INCLUDES) -msoft-float

CFLAGS   := $(COMFLAGS) -std=c99 $(CFLAGS)
CXXFLAGS := $(COMFLAGS) -std=c++14 -Wno-sign-compare -faligned-new -DEXCEPTIONS=1 -Wno-delete-non-virtual-dtor
ASFLAGS  := $(ASFLAGS) -Iinclude -I$(O)/include -m64 -MD -MP -DHW_$(HW) -include param.h

CXXRUNTIME =

ALL :=
all:

define SYSCALLGEN
	@echo "  GEN    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(PYTHON) tools/syscalls.py $(1) kernel/*.cc > $@.tmp
	$(Q)cmp -s $@.tmp $@ || mv $@.tmp $@
endef

ifeq ($(PLATFORM),xv6)
# include net/Makefrag
include kernel/Makefrag
include lib/Makefrag
endif
include libutil/Makefrag
include bin/Makefrag
include tools/Makefrag
include metis/Makefrag

$(O)/%.o: %.c $(O)/sysroot
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(O)/%.o: %.cc $(O)/sysroot
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: $(O)/%.cc $(O)/sysroot
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) -c -o $@ $<

$(O)/%.o: %.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

$(O)/%.o: $(O)/%.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<

$(O)/bootx64.efi: $(O)/kernel.elf
	objcopy --set-section-alignment *=4096 -j .text -j .rodata -j .stapsdt.base -j .kmeta -j .data -j .bss \
		-O pei-x86-64 $< $@

#libunwind
LIBUNWIND_SRC = libunwind/src
LIBUNWIND_OBJ = $(O)/libunwind
LIBUNWIND_SRC_C_FILES := $(wildcard $(LIBUNWIND_SRC)/*.c)
LIBUNWIND_SRC_CXX_FILES := $(wildcard $(LIBUNWIND_SRC)/*.cpp)
LIBUNWIND_SRC_S_FILES := $(wildcard $(LIBUNWIND_SRC)/*.S)
LIBUNWIND_OBJ_FILES := $(patsubst $(LIBUNWIND_SRC)/%.c,$(LIBUNWIND_OBJ)/%.o,$(LIBUNWIND_SRC_C_FILES)) \
						$(patsubst $(LIBUNWIND_SRC)/%.cpp,$(LIBUNWIND_OBJ)/%.o,$(LIBUNWIND_SRC_CXX_FILES)) \
						$(patsubst $(LIBUNWIND_SRC)/%.S,$(LIBUNWIND_OBJ)/%.o,$(LIBUNWIND_SRC_S_FILES))
LIBUNWIND_FLAGS = -D_LIBUNWIND_IS_BAREMETAL -D_LIBUNWIND_HAS_NO_THREADS -Ilibunwind/include -mcmodel=kernel
$(LIBUNWIND_OBJ)/%.o: $(LIBUNWIND_SRC)/%.cpp
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(CXXFLAGS) $(LIBUNWIND_FLAGS) -c -o $@ $<
$(LIBUNWIND_OBJ)/%.o: $(LIBUNWIND_SRC)/%.c
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) $(LIBUNWIND_FLAGS) -c -o $@ $<
$(LIBUNWIND_OBJ)/%.o: $(LIBUNWIND_SRC)/%.S
	@echo "  CC     $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(ASFLAGS) -c -o $@ $<
$(O)/libunwind.a: $(LIBUNWIND_OBJ_FILES)
	@echo "  AR	   $@"
	$(Q)$(AR) rcs $@ $^

#libcxxabi
LIBCXXABI_SRC = libcxxabi/src
LIBCXXABI_OBJ = $(O)/libcxxabi
LIBCXXABI_SRC_C_FILES := $(wildcard $(LIBCXXABI_SRC)/*.c)
LIBCXXABI_SRC_CXX_FILES := $(wildcard $(LIBCXXABI_SRC)/*.cpp)
LIBCXXABI_OBJ_FILES := $(patsubst $(LIBCXXABI_SRC)/%.c,$(LIBCXXABI_OBJ)/%.o,$(LIBCXXABI_SRC_C_FILES)) \
						$(patsubst $(LIBCXXABI_SRC)/%.cpp,$(LIBCXXABI_OBJ)/%.o,$(LIBCXXABI_SRC_CXX_FILES))
LIBCXXABI_FLAGS = -D_NOEXCEPT=noexcept -DLIBCXXABI_USE_LLVM_UNWINDER -Ilibcxx/include -Ilibcxxabi/include -mcmodel=kernel # -D_LIBCPP_BUILDING_LIBRARY
$(LIBCXXABI_OBJ)/%.o: $(LIBCXXABI_SRC)/%.cpp
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(LIBCXXABI_FLAGS) $(CXXFLAGS) -c -o $@ $<
$(LIBCXXABI_OBJ)/%.o: $(LIBCXXABI_SRC)/%.c
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) $(LIBCXXABI_FLAGS) -c -o $@ $<
$(O)/libcxxabi.a: $(LIBCXXABI_OBJ_FILES)
	@echo "  AR	   $@"
	$(Q)$(AR) rcs $@ $^

#libcxx
LIBCXX_SRC = libcxx/src
LIBCXX_OBJ = $(O)/libcxx
LIBCXX_SRC_C_FILES := $(wildcard $(LIBCXX_SRC)/*.c)
LIBCXX_SRC_CXX_FILES := $(wildcard $(LIBCXX_SRC)/*.cpp)
LIBCXX_OBJ_FILES := $(patsubst $(LIBCXX_SRC)/%.c,$(LIBCXX_OBJ)/%.o,$(LIBCXX_SRC_C_FILES)) \
						$(patsubst $(LIBCXX_SRC)/%.cpp,$(LIBCXX_OBJ)/%.o,$(LIBCXX_SRC_CXX_FILES))
LIBCXX_FLAGS = -D_NOEXCEPT=noexcept -D_LIBCPP_BUILDING_LIBRARY -Ilibcxx/include -mcmodel=kernel # -D_LIBCPP_BUILDING_LIBRARY
$(LIBCXX_OBJ)/%.o: $(LIBCXX_SRC)/%.cpp
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(LIBCXX_FLAGS) $(CXXFLAGS) -c -o $@ $<
$(LIBCXX_OBJ)/%.o: $(LIBCXX_SRC)/%.c
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) $(LIBCXX_FLAGS) -c -o $@ $<
$(O)/libcxx.a: $(LIBCXX_OBJ_FILES)
	@echo "  AR	   $@"
	$(Q)$(AR) rcs $@ $^


#compiler-rt
COMPILERRT_SRC = compiler-rt/lib/builtins
COMPILERRT_OBJ = $(O)/compiler-rt
COMPILERRT_SRC_C_FILES := $(wildcard $(COMPILERRT_SRC)/*.c)
COMPILERRT_SRC_CXX_FILES := $(wildcard $(COMPILERRT_SRC)/*.cpp)
COMPILERRT_OBJ_FILES := $(patsubst $(COMPILERRT_SRC)/%.c,$(COMPILERRT_OBJ)/%.o,$(COMPILERRT_SRC_C_FILES)) \
						$(patsubst $(COMPILERRT_SRC)/%.cpp,$(COMPILERRT_OBJ)/%.o,$(COMPILERRT_SRC_CXX_FILES))
COMPILERRT_FLAGS = -Ilibcxx/include
$(COMPILERRT_OBJ)/%.o: $(COMPILERRT_SRC)/%.cpp
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CXX) $(COMPILERRT_FLAGS) $(CXXFLAGS) -c -o $@ $<
$(COMPILERRT_OBJ)/%.o: $(COMPILERRT_SRC)/%.c
	@echo "  CXX    $@"
	$(Q)mkdir -p $(@D)
	$(Q)$(CC) $(CFLAGS) $(COMPILERRT_FLAGS) -c -o $@ $<
$(O)/compiler-rt.a: $(COMPILERRT_OBJ_FILES)
	@echo "  AR	   $@"
	$(Q)$(AR) rcs $@ $^



# Construct an alternate "system include root" by copying headers from the host that are part of
# C++'s freestanding implementation.  These headers are distributed across several directories, so
# we reproduce that directory tree here and let GCC use its standard (large) include path, but
# re-rooted at this new directory.
$(O)/sysroot: include/host_hdrs.hh
	rm -rf $@.tmp $@
	mkdir -p $@.tmp
	tar c $$($(CXX) -E -H -std=c++0x -ffreestanding $< -o /dev/null 2>&1 \
		| awk '/^[.]/ {print $$2}') | tar xC $@.tmp
	mv $@.tmp $@

##
## qemu
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
qemu-grub: $(O)/boot.img
	$(eval QEMUKERNEL = )
	$(QEMUCOMMAND) $<
qemu-test: $(KERN)
	$(eval QEMUAPPEND += %/bin/unittests.sh)
	timeout --foreground 15m $(QEMUCOMMAND)

codex: $(KERN)

##
## disk images
##
$(O)/writeok:
	$(Q)echo >$@
$(O)/fs.part: $(O)/tools/mkfs $(FSCONTENTS)
	@echo "  GEN    $@"
	$(Q)$(O)/tools/mkfs $@.tmp $(O)/fs
	mv $@.tmp $@
$(O)/boot.fat: $(O)/kernel.elf grub/grub.cfg grub/grub.efi $(O)/writeok
	@echo "  GEN    $@"
	$(Q)dd if=/dev/zero of=$@ bs=4096 count=66560 2> /dev/null
	$(Q)mkfs.fat -F 32 -s 8 -S 512 $@ > /dev/null
	$(Q)mmd -i $@ ::EFI
	$(Q)mmd -i $@ ::EFI/BOOT
	$(Q)mcopy -i $@ grub/grub.efi ::EFI/BOOT/BOOTX64.EFI
	$(Q)mcopy -i $@ grub/grub.cfg ::grub.cfg
	$(Q)mcopy -i $@ $(O)/kernel.elf ::ward
	$(Q)mcopy -i $@ $(O)/writeok ::writeok
$(O)/boot.img: $(O)/boot.fat $(O)/fs.part grub/boot.img grub/core.img
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
$(O)/boot.vhdx: $(O)/boot.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vhdx $< $@
$(O)/boot.vdi: $(O)/boot.img
	@echo "  GEN    $@"
	$(Q)qemu-img convert -f raw -O vdi $< $@

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
## general commands
##

.PRECIOUS: $(O)/%.o
.PHONY: clean qemu qemu-gdb qemu-grub qemu-test

clean:
	rm -fr $(O)

all: $(ALL)

