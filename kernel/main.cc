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
#include "sampler.h"
#include "nospec-branch.hh"

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
void initpmc(void);
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
  //initwd();                     // Requires initnmi
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

extern "C" u128 do_reverse_syscall(void(*target)());
extern "C" u64 measure_branch();
extern "C" u64 time_branch();
extern "C" void target_aaa();
extern "C" void target_bbb();
extern "C" void spectre2_kk();
extern "C" void spectre2_uk();
extern "C" void spectre2_ku();
extern "C" void spectre2_uu();
extern "C" void spectre2_uu_train();
extern "C" void spectre2_uu_nosyscall();
extern "C" void time_branches_u();
extern "C" u64 time_branches_k();
extern "C" void fill_return_buffer();
extern "C" u128 time_sysret();
extern "C" u64 time_sysret_baseline();
extern "C" void time_syscall_user();
extern "C" u128 time_syscall_baseline();

extern char syscall_over[];
extern char usercode_segment[];

u64* ENTRY_TIMES;
u64 ENTRY_COUNT;

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
  //initwd();                // Requires initnmi
  //initpmc();
  initattack(); // for spectre demo

  idleloop();

  panic("Unreachable");
}

void ibrs_test() {
  extern u64 kpml4[];

  u64 old_cr3 = rcr3();
  lcr3(v2p(kpml4));

  char* usercode = kalloc("usercode");
  char* branch_target_page = zalloc("branch_target");
  char* user_stack = zalloc("user_stack");

  memmove(usercode, usercode_segment, 4096);

  u64* pml3 = (u64*)zalloc("");
  u64* pml2 = (u64*)zalloc("");
  u64* pml1 = (u64*)zalloc("");
  pml1[1] = v2p(usercode) | PTE_A | PTE_D | PTE_U | PTE_P;
  pml1[3] = v2p(branch_target_page) | PTE_A | PTE_D | PTE_W | PTE_U | PTE_P;
  pml1[4] = v2p(user_stack) | PTE_A | PTE_D | PTE_W | PTE_U | PTE_P;
  pml2[0] = v2p(pml1) | PTE_A | PTE_D | PTE_U | PTE_W | PTE_P;
  pml3[0] = v2p(pml2) | PTE_A | PTE_D | PTE_U | PTE_W | PTE_P;
  kpml4[0] = v2p(pml3) | PTE_A | PTE_D | PTE_U | PTE_W | PTE_P;

  int num_iterations = 1024;
  ENTRY_TIMES = (u64*)kalloc("times", 8 * num_iterations);
  u64* DIV_COUNTS = (u64*)kalloc("div_counts", 8 * num_iterations);

  u64 aaa = 0x1000 + ((char*)target_aaa - (char*)usercode_segment);
  u64 bbb = 0x1000 + ((char*)target_bbb - (char*)usercode_segment);

  void(*targets[])()  = {spectre2_kk, nullptr, spectre2_uu, spectre2_uu_nosyscall, spectre2_uk, spectre2_ku, nullptr };
  const char* target_names[] = {"k-s-k", "k-*-k", "u-s-u", "u-*-u", "u-s-k", "k-s-u", "k-!-k" };

  u64 counters[][3] = {
    { // Ice Lake
      0x0914 | PERF_SEL_USR | PERF_SEL_OS | (1ull << PERF_SEL_CMASK_SHIFT) | PERF_SEL_EDGE,
      0x02C5 | PERF_SEL_USR | PERF_SEL_OS, // BR_MISP_RETIRED.INDIRECT_CALL
      0x80C4 | PERF_SEL_USR | PERF_SEL_OS // BR_INST_RETIRED.INDIRECT
    },
    { // Skylake
      0x0114 | PERF_SEL_USR | PERF_SEL_OS | (1ull << PERF_SEL_CMASK_SHIFT) | PERF_SEL_EDGE,
      0x02C5 | PERF_SEL_USR | PERF_SEL_OS, // BR_MISP_RETIRED.NEAR_CALL
      0x02C4 | PERF_SEL_USR | PERF_SEL_OS // BR_INST_RETIRED.NEAR_CALL
    },
    { // Zen / Zen+ / Zen 2 
      0x0D3 | PERF_SEL_USR | PERF_SEL_OS, // Div Cycles Busy count
      0x0CA | PERF_SEL_USR | PERF_SEL_OS, // Retired Indirect Branch Instructions Mispredicted
      0x0C2 | PERF_SEL_USR | PERF_SEL_OS  // Retired Branch Instructions
    },
    { // Zen 3
      0x0D3 | PERF_SEL_USR | PERF_SEL_OS, // Div Cycles Busy count
      0x0CA | PERF_SEL_USR | PERF_SEL_OS, // Retired Indirect Branch Instructions Mispredicted
      0x0C2 | PERF_SEL_USR | PERF_SEL_OS  // Retired Branch Instructions
    }
  };
  const char* counter_names[] = {
    "ARITH.DIVIDER_ACTIVE>0         ", 
    "BR_MISP_RETIRED.INDIRECT_CALL>0", 
    "BR_INST_RETIRED.INDIRECT==1    "
  };

  //cprintf("IA32_ARCH_CAPABILITIES = %lx\n", readmsr(0x10A));
  writemsr(MSR_LSTAR, (u64)syscall_over);
  for (int ibrs_mode = 0; ibrs_mode < 2*0; ibrs_mode++) {
    writemsr(0x48, (readmsr(0x48)&0xfffffffe) | ibrs_mode); // IA32_SPEC_CTRL: Enable IBRS
    cprintf("IA32_SPEC_CTRL = %lx\n", readmsr(0x48));
    //assert(readmsr(0x10A) & 0x2);

    for (int counter = 0; counter < 2; counter++) {
      int family = cpuid::model().family;
      int model = cpuid::model().model;
      if (cpuid::vendor_is_intel()) {
        if (family == 6 && (model == 0x7e || model == 0x6a || model == 0x6c)) // Ice Lake
          configure_perf_counter(counters[0][counter]);
        else if (family == 6 && (model == 0x4e || model == 0x5e | model == 0x55)) // Skylake
          configure_perf_counter(counters[1][counter]);
        else {
          cprintf("WARN: Unable to recognize Intel CPU Model. Using Skylake performance counter configuration\n");
          configure_perf_counter(counters[1][counter]);
        }
      } else if (cpuid::vendor_is_amd()) {
        if (family == 0x17) {
          configure_perf_counter(counters[2][counter]);
        } else if (family == 0x19 && model == 33) {
          configure_perf_counter(counters[3][counter]);
        } else {
          cprintf("WARN: Unable to recognize AMD CPU Model\n");
          break;
        }
      } else {
        cprintf("WARN: Unknown vendor\n");
        break;
      }

      for (int kind = 0; kind < 7; kind++) {
        for (int iteration = 0; iteration < num_iterations; iteration++) {
          u128 result;
          u64 t1=0, t2=0;

          if (kind == 0 || kind == 1 || kind == 5 || kind == 6) {
            *(u64*)branch_target_page = aaa;
            for (int j = 0; j < 1024; j++)
              ((u64(*)())(0x1000 + (char*)measure_branch - usercode_segment))();
          } else if (kind == 2) {
            result = do_reverse_syscall(spectre2_uu_train);
            t2 = rdtscp_and_serialize();
            t1 = (u64)result;
          }

          if (kind == 1) {
            result = rdtsc();
          } else if (kind == 6) {
            result = rdtsc();
            indirect_branch_prediction_barrier();
          } else {
            result = do_reverse_syscall(targets[kind]);
          }

          if (kind != 2) {
            t2 = rdtscp_and_serialize();
            t1 = (u64)result;
          }

          if (kind == 2 || kind == 3 || kind == 5) {
            DIV_COUNTS[iteration] = result >> 64;
          } else {
            *(u64*)branch_target_page = bbb;
            DIV_COUNTS[iteration] = ((u64(*)())(0x1000 + (char*)measure_branch - usercode_segment))();
          }

          ENTRY_TIMES[iteration] = t2 - t1;
        }

        u64 avg = 0;
        for (int i = 128; i < num_iterations; i++)
          avg += ENTRY_TIMES[i];
        avg /= num_iterations - 128;

        int count[2] = {0, 0};
        int with_div[2] = {0, 0};
        for (int i = 128; i < num_iterations; i++) {
          int is_fast = ENTRY_TIMES[i] <= avg*3/2 ? 1 : 0;
          count[is_fast]++;
          if (((counter == 0 || counter == 1) && DIV_COUNTS[i] > 0) || (counter == 2 && DIV_COUNTS[i] == 1))
            with_div[is_fast]++;
        }

        int v_slow = with_div[0] * 100000 / (count[0] > 0 ? count[0] : 0xffffffff);
        int v_fast = with_div[1] * 100000 / (count[1] > 0 ? count[1] : 0xffffffff);
        cprintf("%s [%s] fast: %4d.%04d%% (%4d/%4d)    ", 
          counter_names[counter], target_names[kind], v_fast / 1000, v_fast % 1000, with_div[1], count[1]);
        if (count[0] > 0)
          cprintf("slow: %4d.%04d%% (%4d/%4d)    ", v_slow / 1000, v_slow % 1000, with_div[0], count[0]);
        else
          cprintf("                                ");
        cprintf("average = %4lu cycles   [", avg);

        for (int i = 340; i < 340 + 96; i++) {
          if (ENTRY_TIMES[i] <= avg*3/2)
            cprintf(DIV_COUNTS[i] > 0 ? "." : " ");
          else
            cprintf("*");
        }

        cprintf("]\n");
      }



      // u64 time_sums[2] = {0, 0};
      // for (int mistrain = 0; mistrain < 2; mistrain++) {
      //   for (int iteration = 0; iteration < num_iterations; iteration++) {
      //     *(u64*)branch_target_page = mistrain ? aaa : bbb;
      //     time_sums[mistrain] += do_reverse_syscall(time_branches_u) >> 64;
      //   }
      // }
      // cprintf("[u] trained: %3lu cycles, mistrained: %3lu cycles\n", time_sums[0] / num_iterations, time_sums[1] / num_iterations);

      // for (int mistrain = 0; mistrain < 2; mistrain++) {
      //   for (int iteration = 0; iteration < num_iterations; iteration++) {
      //     *(u64*)branch_target_page = mistrain ? aaa : bbb;
      //     time_sums[mistrain] += time_branches_k();
      //   }
      // }
      // cprintf("[k] trained: %3lu cycles, mistrained: %3lu cycles\n", time_sums[0] / num_iterations, time_sums[1] / num_iterations);

      // for (int i = 340; i < 354; i++)
      //   cprintf("%ld \t(%ld)\n", ENTRY_TIMES[i], DIV_COUNTS[i]);   
    }
  }

  ENTRY_COUNT = 0xffffffff;
  kfree(ENTRY_TIMES);

  extern const char mds_clear_cpu_buffers_ds[];
  u64 pml4_value = (rcr3() | CR3_NOFLUSH) & mycpu()->cr3_mask;
  u64 alt_pml4_value = (v2p(kalloc("alt_kpml4")) | CR3_NOFLUSH | 14) & mycpu()->cr3_mask;
  memcpy(p2v(alt_pml4_value&0x00fffff000), p2v(pml4_value&0x00fffff000), PGSIZE);

  const char* operation_names[] = { 
    "nop               ",
    "verw              ",
    "IBPB              ",
    "FILL_RETURN_BUFFER",
    "lfence            ",
    "retpoline         ",
    "indirect call     ",
    "indirect call+ibrs",
    "mov %cr3          ",
    "syscall baseline  ",
    "syscall           ",
    "sysret baseline   ",
    "sysret            "
  };
  u64 min_nop_time = 0;
  for (int op = 0; op <= 12; op++) {
    // IA32_SPEC_CTRL: Disable then re-enable IBRS
    if (op == 6) writemsr(0x48, (readmsr(0x48)&0xfffffffe));
    if (op == 7) writemsr(0x48, (readmsr(0x48)&0xfffffffe) | 1);

    u64 sum = 0, mint = 999999999, maxt = 0;
    for (int i = 0; i < 1000000; i++) {
      u64 t;

      switch (op) {
        case 0:
          t = serialize_and_rdtsc();
          t = rdtscp_and_serialize() - t;
          break;
        case 1:
          t = serialize_and_rdtsc();
          asm volatile ("verw (mds_clear_cpu_buffers_ds)");
          t = rdtscp_and_serialize() - t;
          break;
        case 2:
          t = serialize_and_rdtsc();
          indirect_branch_prediction_barrier();
          t = rdtscp_and_serialize() - t;
          break;
        case 3:
          t = serialize_and_rdtsc();
          fill_return_buffer();
          t = rdtscp_and_serialize() - t;
          break;
        case 4:
          t = serialize_and_rdtsc();
          barrier_nospec();
          t = rdtscp_and_serialize() - t;
          break;
        case 5:
          t = serialize_and_rdtsc();
          asm volatile ("movq %0, %%r11; call __x86_indirect_thunk_r11" :: "r" (bbb) : "r11");
          t = rdtscp_and_serialize() - t;
          break;
        case 6:
        case 7:
          t = serialize_and_rdtsc();
          asm volatile ("movq %0, %%r11; callq *%%r11" :: "r" (bbb) : "r11");
          t = rdtscp_and_serialize() - t;
          break;
        case 8:
          t = serialize_and_rdtsc();
          lcr3(alt_pml4_value);
          t = rdtscp_and_serialize() - t;
          lcr3(pml4_value);
          break;
        case 9:
          t = (u64)time_syscall_baseline();
          t = rdtscp_and_serialize() - t;
          t += min_nop_time;
          break;
        case 10:
          t = (u64)do_reverse_syscall(time_syscall_user);
          t = rdtscp_and_serialize() - t;
          t += min_nop_time;
          break;
        case 11:
          t = time_sysret_baseline();
          t += min_nop_time;
          break;
        case 12:
          t = time_sysret() >> 64;
          t += min_nop_time;
          break;
      }

      sum += t;
      mint = t < mint ? t : mint;
      maxt = t > maxt ? t : maxt;
    }
    if (op == 0) {
      min_nop_time = mint;
    } else {
      mint -= min_nop_time;
      maxt -= min_nop_time;
      sum -= min_nop_time * 1000000;
    }
    cprintf("%s = %5ld (min = %5ld, max = %5ld)\n", operation_names[op], sum / 1000000, mint, maxt);
  }

  writemsr(MSR_LSTAR, (u64)&sysentry);
  kpml4[0] = 0;
  lcr3(old_cr3);
}

//SYSCALL
long
sys_ibrs_test(void)
{
  ensure_secrets();
  pause_other_cpus_and_call(ibrs_test);
  return 0;
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
