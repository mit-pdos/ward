#include "types.h"
#include "multiboot.hh"
#include "kernel.hh"
#include "spinlock.hh"
#include "kalloc.hh"
#include "cpu.hh"
#include "amd64.h"
#include "hwvm.hh"
#include "condvar.hh"
#include "proc.hh"
#include "apic.hh"
#include "codex.hh"
#include "mfs.hh"
#include "cpuid.hh"

void vga_boot_animation();
void initmultiboot(u64 mbmagic, u64 mbaddr);
void debugmultiboot(u64 mbmagic, u64 mbaddr);
void initpic(void);
void initextpic(void);
void inituart(void);
void inituartcons(void);
void initcga(void);
void initvga(void);
void initconsole(void);
void initdirectmap();
void initpg(struct cpu *c);
void inithz(void);
void cleanuppg(void);
void inittls(struct cpu *);
void initcodex(void);
void inittrap(void);
void initvectoredtrap(void);
void initfpu(void);
void initmsr(void);
void initseg(struct cpu *);
void initphysmem(void);
void initpercpu(void);
void initpageinfo(void);
void initkalloc(void);
void initrcu(void);
void initproc(void);
void initinode(void);
void initide(void);
void initmemide(void);
void initpartition(void);
void inituser(void);
void initsamp(void);
void inite1000(void);
void initahci(void);
void initpci(void);
void initnet(void);
void initsched(void);
void initlockstat(void);
void initidle(void);
void initcpprt(void);
void initcmdline(void);
void initrefcache(void);
void initacpitables(void);
void initnuma(void);
void initcpus(void);
void initlapic(void);
void initiommu(void);
void initacpi(void);
void initwd(void);
void initdev(void);
void inithpet(void);
void inittsc(void);
void initrtc(void);
void initmfs(void);
void initvfs(void);
void idleloop(void);
void inithotpatch(void);
void initattack(void);

#define IO_RTC  0x70

static std::atomic<int> bstate;
static cpuid_t bcpuid;

void
mpboot(void)
{
  initseg(&cpus[bcpuid]);
  inittls(&cpus[bcpuid]);       // Requires initseg
  initpg(&cpus[bcpuid]);

  // Call per-CPU static initializers.  This is the per-CPU equivalent
  // of the init_array calls in cmain.
  extern void (*__percpuinit_array_start[])(size_t);
  extern void (*__percpuinit_array_end[])(size_t);
  for (size_t i = 0; i < __percpuinit_array_end - __percpuinit_array_start; i++)
      (*__percpuinit_array_start[i])(bcpuid);

  initlapic();
  inittsc();
  initfpu();
  initmsr();
  initsamp();
  initidle();
  initwd();                     // Requires initnmi
  bstate.store(1);
  idleloop();
}

static void
warmreset(u32 addr)
{
  volatile u16 *wrv;

  // "The BSP must initialize CMOS shutdown code to 0AH
  // and the warm reset vector (DWORD based at 40:67) to point at
  // the AP startup code prior to the [universal startup algorithm]."
  outb(IO_RTC, 0xF);  // offset 0xF is shutdown code
  outb(IO_RTC+1, 0x0A);
  wrv = (u16*)p2v(0x40<<4 | 0x67);  // Warm reset vector
  wrv[0] = 0;
  wrv[1] = addr >> 4;
}

static void
rstrreset(void)
{
  volatile u16 *wrv;

  // Paranoid: set warm reset code and vector back to defaults
  outb(IO_RTC, 0xF);
  outb(IO_RTC+1, 0);
  wrv = (u16*)p2v(0x40<<4 | 0x67);
  wrv[0] = 0;
  wrv[1] = 0;
}

static void
bootothers(void)
{
  extern u8 _bootother_start[];
  extern u64 _bootother_size;
  extern void (*apstart)(void);
  char *stack;
  u8 *code;

  // Write bootstrap code to unused memory at 0x7000.
  // The linker has placed the image of bootother.S in
  // _binary_bootother_start.
  code = (u8*) p2v(0x7000);
  memmove(code, _bootother_start, _bootother_size);

  for (int i = 0; i < ncpu; ++i) {
    if(i == myid())  // We've started already.
      continue;
    struct cpu *c = &cpus[i];

    warmreset(v2p(code));

    // Tell bootother.S what stack to use and the address of apstart;
    // it expects to find these two addresses stored just before
    // its first instruction.
    stack = (char*) kalloc("kstack", KSTACKSIZE);
    *(u32*)(code-4) = (u32)v2p(&apstart);
    *(u64*)(code-12) = (u64)stack + KSTACKSIZE;
    // bootother.S sets this to 0x0a55face early on
    *(u32*)(code-64) = 0;

    bstate.store(0);
    bcpuid = c->id;
    lapic->start_ap(c, v2p(code));
#if CODEX
    codex_magic_action_run_thread_create(c->id);
#endif
    // Wait for cpu to finish mpmain()
    while(bstate.load() == 0)
      nop_pause();
    rstrreset();
  }
}

void
cmain(u64 mbmagic, u64 mbaddr)
{
  // Make cpus[0] work.  CPU 0's percpu data is pre-allocated directly
  // in the image.  *cpu and such won't work until we inittls.
  percpu_offsets[0] = __percpu_start;

  // Initialize output
  inithz();
  inituart();              // Requires inithz
  initmultiboot(mbmagic, mbaddr);
  initcmdline();           // Requires initmultiboot
  initphysmem();           // Requires initmultiboot
  initdirectmap();         // Requires initmultiboot, initphysmem
  initvga();               // Requires initcmdline, initdirectmap

  // Initialize trap handling
  initseg(&cpus[0]);
  inittls(&cpus[0]);       // Requires initseg
  inittrap();

  initpg(&cpus[0]);        // Requires initphysmem, initvga
  vga_boot_animation();    // Requires initpg
  initacpitables();        // Requires initpg, inittls
  initlapic();             // Requires initpg, inithz
  initnuma();              // Requires initacpitables, initlapic
  initpercpu();            // Requires initnuma
  initcpus();              // Requires initnuma, initpercpu, suggests initacpitables
  initpic();               // interrupt controller
  initiommu();             // Requires initlapic
  initextpic();            // Requires initpic
  initvectoredtrap();      // Requires initpercpu
  // Interrupt routing is now configured

  inituartcons();          // Requires interrupt routing
  initcga();
  initpageinfo();          // Requires initnuma

  // Some global constructors require mycpu()->id (via myid()) which
  // we setup in inittls.  Some require dynamic allocation of large
  // memory regions (e.g., for hash tables), which requires
  // initpageinfo and needs to happen *before* initkalloc.  (Note that
  // gcc 4.7 eliminated the .ctors section entirely, but gcc has
  // supported .init_array for some time.)  Note that this will
  // implicitly initialize CPU 0's per-CPU objects as well.
  extern void (*__init_array_start[])(int, char **, char **);
  extern void (*__init_array_end[])(int, char **, char **);
  for (size_t i = 0; i < __init_array_end - __init_array_start; i++)
      (*__init_array_start[i])(0, nullptr, nullptr);

  inithotpatch();
  inithpet();              // Requires initacpitables
  inittsc();               // Requires inithpet
  initfpu();               // Requires nothing
  initmsr();               // Requires nothing
  initkalloc();            // Requires initpageinfo
  initproc();
  initsched();
  inituser();
  initidle();
  initgc();
  initrefcache();          // Requires initsched
  initconsole();
  initsamp();
  initlockstat();
  initacpi();              // Requires initacpitables, initkalloc?
  inite1000();             // Before initpci
#if AHCIIDE
  initahci();
#endif
  initpci();               // Suggests initacpi
  initnet();
  initrtc();               // Requires inithpet
  initdev();
  initide();
  initmemide();
  initpartition();
  initinode();
  initmfs();

  // XXX hack until mnodes can load from disk
  extern void mfsload();
  mfsload();
  initvfs();

#if CODEX
  initcodex();
#endif
  bootothers();
  cleanuppg();             // Requires bootothers
  initcpprt();
  initwd();                // Requires initnmi
  initattack(); // for spectre demo

  idleloop();

  panic("Unreachable");
}

void
halt(void)
{
  acpi_power_off();

  for (;;);
}

void
paravirtual_exit(int exit_code)
{
  if(exit_code != 0) {
    assert((exit_code & 1) == 1);
    if ((strcmp(cpuid::features().hypervisor_id, "KVMKVMKVM") == 0) ||
        (strcmp(cpuid::features().hypervisor_id, "TCGTCGTCGTCG") == 0)) {
      // Requires `-device isa-debug-exit` flag to QEMU
      outw(0x501, exit_code >> 1);
    } else {
      cprintf("NOTE: paravirtual_exit(%d) but QEMU paravirtualization not detected\n", exit_code);
    }
  }

  acpi_power_off();
  while(1);
}
