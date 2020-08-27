
# How to boot almost anywhere

Ward seeks to boot almost anywhere. To serve that purpose ward is distributed in
two separate forms:

* A [multiboot][3] and [multiboot2][4] compliant ELF file that can co-exist with other
  operating systems in the /boot directory.

* A raw disk image that can be written to a flash drive via dd, or passed as a
  boot drive to QEMU and other hypervisor software

## MBR

Legacy x86 systems boot by copying the first 512 byte sector of the hard drive
to address 0x7c00 and then jumping to that location. At this point the system is
still in 16-bit mode and a considerable amount of bootstraping is required to
switch to 64-bit protected mode and load the rest of the kernel into memory.

This method is mostly obsolete on bare metal systems, but is frequently required
by cloud providers so it still must be supported.

## UEFI

The [Universal Extensible Firmware Interface][1] defines a procedure for loading
kernel binaries from disk and starting them. Unlike the MBR method, UEFI
understands FAT32 filesystems and Microsoft's PE32+ executable format
eliminating the need for a dedicated bootloader.

There are a couple different approaches for handling the executable format
requirement:

* For kernels that don't need to support other boot environments, they can be
  distributed only as PE32+ binaries. These binaries can be produced directly by
  a compiler or converted via objcopy. This is the approach used by Windows.

* To support UEFI and its own bootloader format at the same time, Linux exposes
  a [EFI Stub][2]. This lets the Linux kernel binary masquerade as a PE32+
  binary well enough to get firmware to load it.

* The kernel can be in a completely different binary format, but get chainloaded
  by a UEFI compatible bootloader like GRUB2. This is the approach that Ward
  uses.

## GRUB

GRUB2 provides several different installation methods, but they all either
require being run from within a running system or make undesirable assuptions
about the disk layout. Since Ward wants more flexibility, we assemble our own
disk image by piecing together a couple of components.

### boot.img

This file contains the initial boot code to place into the first sector of the
hard drive (the MBR). It is 512 bytes long, although only the first 440 bytes
should be copied to avoid overwriting partition information.

One important detail is that the GRUB installer actually replaces bytes 92-99 of
this file with the offset (in 512 byte sectors) to the start of the main GRUB
binary.

```bash
cp /usr/lib/grub/i386-pc/boot.img .
python3 -c "print('\x40')" | dd of=boot.img bs=1 seek=92 count=1 conv=notrunc 2> /dev/null
```

### core.img

This file contains the main GRUB installation for MBR systems. It is generated
via the grub-mkimage command and has all necessary modules compiled into it. You
also have the option of passing an early config file to specify the root
partition and where to look for the main config file.

```bash
grub-mkimage -O i386-pc -o core.img -p '/' -c grub/grub-early.cfg \
    biosdisk normal search part_msdos part_gpt fat multiboot multiboot2 gfxmenu echo probe
```

It turns out that boot.img actually only loads the first 512 bytes of core.img,
so more information is required to get the rest loaded into memory. This initial
prefix (called diskboot.img) contains a hardcoded offset and length for the core
file. Note that some documentation describes this as a "block list", but
it is actually only a single entry.

The mkimage command sets the length correctly, but it doesn't actually know the
disk offset that core.image will be placed at so that must be handled
separately. In particular, we must overwrite bytes 500-503 with the correct
offset in sectors:

```bash
python3 -c "print('\x41')" | dd of=core.img bs=1 seek=500 count=1 conv=notrunc 2> /dev/null
```

### grub.efi

The previous two files are sufficient for booting in MBR mode, but we would also
like to support UEFI. This turns out to be a bit simpler, and no binary patching
is required:

```bash
grub-mkimage -O x86_64-efi -o $@ -p '/' -c grub/grub-early.cfg \
		normal search part_msdos part_gpt fat multiboot multiboot2 gfxmenu echo video probe
```

## Disk layout

The resulting .efi file should be renamed to bootx64.efi and placed in the
/efi/boot directory of a bootable FAT32 partition on the disk.


[1]: https://uefi.org/sites/default/files/resources/UEFI_Spec_2_8_A_Feb14.pdf
[2]: https://www.kernel.org/doc/html/latest/admin-guide/efi-stub.html
[3]: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
[4]: https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
