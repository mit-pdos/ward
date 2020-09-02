Ward Artifact
=============
## Overview

* System Requirements
* Getting Started (5 minutes)
* Compile Ward (5 minutes)
* Boot Ward (5 minute)
* Run Experiments (10 minutes)
* Validate Results (10 minutes)
* Reuse beyond paper

## 0. System Requirements
* A recent x86-64 processor with support for hardware virtualization
  - Almost any recent laptop/desktop processor should suffice
  - In rare cases you might have to enable "Intel VT-x" or "AMD Virtualization" in the BIOS
* A Linux environment that is *not* inside a VM.
  - This guide has been tested on fresh Ubuntu 20.04 install, but other distros should work as well.
* If isn't possible to get a non-virtualized Linux environment, you can try running steps 1-3 inside a Linux VM and steps 4-5 outside it on a Windows/macOS host system:
  - You'd need to clone the git repository on both the host and the guest
  - Then between steps 3 and 4, copy _output/ward.elf_ (or the entire _output_ directory) from the guest to the host

## 1. Getting Started
* Install dependences: `sudo apt-get install git build-essential clang qemu-system-x86 python3-numpy python3-matplotlib`
  - Use an appropriate package manager if not running Ubuntu/Debian.
* Clone our git repository: `git clone https://github.com/mit-pdos/ward && cd ward`
* Checkout the right commit hash: `git checkout <commit-hash>`

## 2. Compile Ward
* Run `make -j` from the root of the repository to compile the Ward kernel.
  - You should see a list of source files scroll by as they are compiled.
  - Once complete, there will be a directory called _output_ which should contain the _ward.elf_ kernel binary.
  - If the command fails you can run `make clean` or delete the output directory to try again.

## 3. Boot Ward
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
* Type `ls` to confirm that the shell is working.
* Finally run `halt` to shutdown the VM.

## 4. Run experiments
* Launch Ward again with `make -j qemu`
  - If you see any "WARN:" messages this indicates that Ward is running in a compatibility mode since some hardware feature is missing. This may cause benchmark numbers to be less representative.
* Run the LEBench microbenchmark suite by typing `lebench`
  - You should see output as each microbenchmark is run:
  ```
  Benchmark (ward),     Off  Best,    On  Best,  Fast  Best,
  ref,                         25,          25,          25,
  getpid,                     188,        1241,         194,
  ...
  ```
* Once complete, copy the printed CSV contents and save them to a file named _lebench-results.csv_.

## 5. Validate results

* Run the plotting script: `python3 tools/lebench-plot.py lebench-results.csv lebench-plot.pdf`
  - This should produce a _lebench-plot.pdf_ file in the current directory.
* Compare the resulting output to Figure 4 in the paper.
  - In particular, confirm the key claim that Ward's mitigation approach (orange bars) generally has lower runtime than running with traditional mitigations (blue bars).
  - Exact numbers will of course differ from the paper version because they were run on different hardware (and not in a VM!)
* Review _bin/lebench.cc_ to understand what each benchmark is doing.

## 6. Reuse beyond the paper

### Try out the Warden tool
* Install Rust compiler via [rustup](https://rustup.rs/).
* Navigate to _tools/warden_.
* While an instance of Ward is running (i.e. started via `make -j qemu`), run Warden: `cargo run`
  - You should see a list of backtraces of where world barriers occurred (green means intentional, the rest are transparent world barriers).

### Try other programs on Ward
Ward can run a small sample of unmodified Linux binaries. However, it supports a very limited selection of syscalls compared to Linux and many more only handle a few common options. But if you want to try:

* Compile a version of your program that statically links against glibc and any other libraries. (Ward doesn't support dynamic linking)
* Copy it to _output/fs/bin_, or add a Makefile rule to do that.
* Modify the FSCONTENTS variable in _bin/Makefrag_ to also reference your program ('$(O)' is an alias for 'output').

### Run on a different hypervisor
* Install mtools: `sudo apt-get install mtools`
* Build disk images via `make -j disks`
  - This should produce _ward.img_, _ward.vhdx_, _ward.vmdk_, and _vard.vdi_ files in the output directory.
* Test your _ward.img_ disk image by running `make -j qemu-grub` or `qemu-system-x86_64 output/ward.img`.
  - You should see similar output as before.
  - The second command uses QEMU's default options which are suboptimal but Ward should still be able boot.
* Take one of the generated disk images and boot them with your chosen hypervisor

Ward disk images have been tested in both VirtualBox (_ward.vdi_) and Hyper-V (_ward.vhdx_). 

### Run on real hardware
* Review the main [README.md](README.md) for general instructions
* You may need to use the _update_ucode_ utility lets you install new microcode on your processor (it gets reset on every system restart)
  - Place the required microcode file in the _intel-ucode_ directory, it will get copied over to the RAM disk when compiling
  - To revert to earlier microcode, `reboot` the system
* Pipe the output of any program to the QRC utility to display it as a QR code: `lebench | qrc`
