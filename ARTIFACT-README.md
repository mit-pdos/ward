Ward Artifact
=============
## Overview

* Getting Started (5 minutes)
* Compile Ward (5 minutes)
* Boot Ward (1 minute)
* Run Experiments (5 minutes)
* Validate Results (5 minutes)
* Reuse beyond paper

## Getting Started
* Ensure your system meets the minimum requirements:
  - An x86-64 processor with support for hardware virtualization. (Any recent Intel / AMD processor should suffice.)
  - A Linux environment that is *not* inside a VM. This guide has been tested on fresh Ubuntu 20.04 install, but other distros should work as well.

* Install dependences: `sudo apt-get install git build-essential clang qemu-system-x86 mtools`
  - Use appropriate package manager if not running Ubuntu/Debian.
  - mtools is optional. It is only needed for making disk images.
* Clone our git repository: `git clone https://github.com/mit-pdos/ward && cd ward`
* Checkout the right commit hash: `git checkout <commit-hash>`

## Compile Ward
* Run `make -j` from the root of the repository to compile everything
  - You should see a list of source files scroll by as they are compiled.
  - Once complete, there will be a directory called _output_ which should contain the _ward.elf_ kernel binary.
  - If the command fails you can run `make clean` or delete the output directory to try again.
* (optional) build disk images via `make -j img vhdx vdi`
  - This should produce _ward.img_, _ward.vhdx_, and _vard.vdi_ files in the output directory.

# Boot Ward
* Run `make -j qemu` to boot Ward inside a QEMU VM.
  - You should see some messages from QEMU/SGABIOS followed by:
  ```
  Booting from ROM...
  Ward UART
  init complete at Thu Aug 27 16:40:33 2020
  sh: can't access tty; job control turned off
  / $
  ```
  - You can quit QEMU pressing 'Ctrl-a x'. (That is the control and A keys pressed at the same time, followed by releasing both and pressing just the X key.)
  - If you see messages like "qemu-system-x86_64: warning: TCG doesn't support requested feature: CPUID.01H:ECX.pcid" then this may indicate your processor is too old or you don't have hardware acceleration configured properly.
* Type `ls` to comfirm that the shell is working.
* Finally run `halt` to shutdown the VM.
* (optional) Try out your _ward.img_ disk image by running `make -j qemu-grub`.
  - This should produce similar output as before.

## Run experiments
* Launch Ward again with `make -j qemu`
* Run the LEBench microbenchmark suite by typing `lebench`
  - You should see output as each microbenchmark is run:
  ```
  Benchmark (ward),     Off  Best,    On  Best,  Fast  Best,
  ref,                         25,          25,          25,
  getpid,                     188,        1241,         194,
  ...
  ```
* Once complete, copy the printed CSV contents and save them to a file on your host system.

## Evaluate results

TODO
