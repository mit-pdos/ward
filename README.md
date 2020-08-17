Ward is a Spectre and Meltdown resistant research operating system based on
[sv6](https://github.com/aclements/sv6) which was in turn based on
[xv6](http://pdos.csail.mit.edu/6.828/xv6).


Building and running Ward in QEMU
--------------------------------

### Linux

You'll need a recent version of clang, GNU make, and QEMU. Generating disk images also
requires mtools. On Ubuntu, this should just be a matter of:

```bash
sudo apt-get install build-essential clang mtools qemu-system-x86
```

Now you should be set to run:

```bash
git clone git@github.com:mit-pdos/ward && cd ward
make -j qemu
```

The makefile also supports generating IMG, VHDX, and VDI disk images. These
images support both MBR and UEFI booting using bundled copies of GRUB, and
should be compatible with a range of hypervisors:

```bash
make -j img vhdx vdi
```

You can see this by running `qemu-system-x86_64 output/ward.img`, but since
QEMU's default settings are rather suboptimal, there is a make target for using
preferred options:

```bash
make -j qemu-grub
```

### OSX
To run Ward on OSX, there are some additional steps that need to be taken:
1. [Install homebrew](https://brew.sh/)
1. Install qemu: `brew install qemu`
1. Install truncate: `brew install truncate`
1. [Install macports](https://www.macports.org/install.php)
1. Install x86 binaries: `sudo port install x86_64-elf-binutils`, `sudo port install x86_64-elf-gcc`
1. Add `TOOLPREFIX = x86_64-elf-` to `config.mk`
1. Build: `make -j`
    * If you get the error `x86_64-elf-ar: libgcc_eh.a: No such file or directory`, you may need to find copies of `libgcc_eh.a` (and/or `libsupc++.a`) elsewhere and copy them to your `x86_64-elf-gcc` directory
1. Run: `make qemu`
    * If you get the error `cannot identify root disk with bus location "ahci0.0p1"`, try `make QEMUAPPEND="root_disk=memide.0" qemu`

Running Ward on real hardware
----------------------------

Make sure you can build and run Ward in QEMU first.

## USB Stick

Use `dd` to copy the contents of `output/ward.img` to a flash drive, and then
select the drive as a temporary boot device from the pre-boot menu.

## GRUB

First copy `output/ward.elf` to your `/boot` directory and then add the
following entry to `/etc/grub.d/40_custom`:

```
menuentry "ward" {
    load_video
    echo 'GRUB: Loading ward...'
    multiboot2 /boot/ward.elf
    echo 'GRUB: Booting ward...'
}
```

## PXELINUX

Add the following entry to your `pxelinux.cfg`:

```
label ward
        say Booting ward...
        menu label ward
        kernel mboot.c32
        append -aout /path/to/ward.elf
```

Supported hardware
------------------

Not much.

sv6 is known to run on five machines: QEMU, a 4 core Intel Core2, a 16
core AMD Opteron 8350, 48 core AMD Opteron 8431, and an 80 core Intel
Xeon E7-8870.  Given the range of these machines, we're optimistic
about sv6's ability to run on other hardware.  sv6 supports both
xAPIC- and x2APIC-based architectures.

For networking, sv6 supports several models of the Intel E1000,
including both PCI and PCI-E models.  If you have an E1000, you'll
probably have to add your specific model number to the table in
`kernel/e1000.cc`, but you probably won't have to do anything else.


Running Ward user programs in Linux
-------------------------------

Much of the sv6 user-space can also be compiled for and run in Linux
using `make HW=linux`.  This will place Linux-compatible binaries in
`o.linux/bin`.

You can also boot a Linux kernel into a pure sv6 user-space!  `make
HW=linux` also builds `o.linux/initramfs`, which is a Linux initramfs
file system containing an sv6 init, sh, ls, and everything else.  You
can boot this on a real machine, or run a super-lightweight Linux VM
in QEMU using

    make HW=linux KERN=path/to/Linux/bzImage/or/vmlinuz qemu
