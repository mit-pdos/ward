# Overview

Ward is a Spectre and Meltdown resistant research operating system based on
[sv6](https://github.com/aclements/sv6) which was in turn based on
[xv6](http://pdos.csail.mit.edu/6.828/xv6).


This page is a general README for getting started with Ward. If you are looking
for the directions for the OSDI '20 artifact evaluation, refer to
[ARTIFACT-README.md](ARTIFACT-README.md).

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

The makefile also supports generating IMG, VHDX, VMDK, and VDI disk
images. These images support both MBR and UEFI booting using bundled copies of
GRUB, and the resulting _ward.img_, _ward.vhdx_, _ward.vmdk_, and _ward.vdi_
should be compatible with a range of hypervisors:

```bash
make -j disks
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

Make sure you can build and run Ward in QEMU first. For each of the following
options you'll need either _ward.elf_ or _ward.img_. These files should be in
the output directory after running `make -j` or `make -j disks` respectively. They
are also automatically generated by each [GitHub CI run](https://github.com/mit-pdos/ward/actions).

### GRUB

First copy _ward.elf_ to your `/boot` directory and then add the
following entry to `/etc/grub.d/40_custom`:

```
menuentry "ward" {
    load_video
    echo 'GRUB: Loading ward...'
    multiboot2 /boot/ward.elf
    echo 'GRUB: Booting ward...'
}
```

### PXELINUX

Add the following entry to your `pxelinux.cfg`:

```
label ward
        say Booting ward...
        menu label ward
        kernel mboot.c32
        append -aout /path/to/ward.elf
```

### Bootable USB Stick

Use `dd` to copy the contents of _ward.img_ to a flash drive (replace sdX with
the device number of your flash drive), and then select the drive as a temporary
boot device from the pre-boot menu:

```bash
$ sudo dd if=output/ward.img of=/dev/sdX bs=4M && sync
```

And if you want to make sure the drive works:

```bash
$ qemu-system-x86_64 /dev/sdX
```

NOTE: For reasons that we don't fully understand, this boot method is less
reliable than the previous options but it still may work for you.
