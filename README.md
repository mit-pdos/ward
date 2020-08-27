Ward is a Spectre and Meltdown resistant research operating system based on
[sv6](https://github.com/aclements/sv6) which was in turn based on
[xv6](http://pdos.csail.mit.edu/6.828/xv6).


Compiling Ward
--------------------------------

### On Linux

You'll need a recent version of clang, GNU make, and QEMU. Generating disk images also
requires mtools. On Ubuntu, this should just be a matter of:

```bash
sudo apt-get install git build-essential clang mtools qemu-system-x86
```

Now you should be set to run:

```bash
git clone https://github.com/mit-pdos/ward && cd ward
make -j
```

At this point, the output directory should now contain _ward.elf_ which is a
multiboot compatible kernel binary that can be loaded by a variety of
bootloaders.

The makefile also supports generating IMG, VHDX, and VDI disk images. These
images support both MBR and UEFI booting using bundled copies of GRUB, and the
resulting _ward.img_, _ward.vhdx_, and _ward.vdi_ should be compatible with a
range of hypervisors:

```bash
make -j img vhdx vdi
```

Running Ward in a VM
----------------------------

To launch inside QEMU just run...

```bash
make -j qemu
```
(To exit, press Ctrl-a x.)

Alternatively, if you want to test the generated _ward.img_ disk image, you can run:

```bash
make -j qemu-grub
```

The simplest command would actually just be `qemu-system-x86_64 output/ward.img`, however
QEMU's default settings are rather suboptimal (no hardware acceleration, 128MB
of RAM, etc.)


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
