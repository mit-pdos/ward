#include "string.h"
#include "amd64.h"
#include "kernel.hh"
#include "uefi.hh"
#include "multiboot.hh"
#include "cmdline.hh"
#include "amd64.h"

efi_guid efi_acpi20_table_guid = {0x8868e871,0xe4f1,0x11d3, {0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81}};
efi_guid efi_acpi10_table_guid = {0xeb9d2d30,0x2d88,0x11d3, {0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d}};

void multiboot2_early(u64 mbaddr, multiboot_saved* mb);

extern u64 physical_address_offset;

extern "C" u64 efi_cmain (u64 mbmagic, u64 mbaddr)
{
  u64 offset;
  __asm volatile("call 1f; 1: pop %0; sub $(1b-%a1), %0" : "=r" (offset) : "i"(KCODE));
  physical_address_offset = offset;

  multiboot_saved* mb = (multiboot_saved*)v2p(&multiboot);
  multiboot2_early(mbaddr, mb);
  efi_system_table* system_table = (efi_system_table*)mb->efi_system_table;

  auto get_memory_map = system_table->boot_services->get_memory_map;
  auto exit_boot_services = system_table->boot_services->exit_boot_services;

  // u16 hello_world[] = {'H', 'e', 'l', 'l', 'o', '\n', 0};
  // (system_table->console_out_prot->output_string)(system_table->console_out_prot, hello_world);

  for (int i = 0; i < system_table->num_table_entries; i++) {
    if (system_table->configuration_table[i].guid == efi_acpi10_table_guid) {
      memcpy(mb->acpi_rsdpv1, system_table->configuration_table[i].table, 20);
      mb->flags |= MULTIBOOT2_FLAG_ACPI_RSDP_V1;
    } else if (system_table->configuration_table[i].guid == efi_acpi20_table_guid) {
      memcpy(mb->acpi_rsdpv2, system_table->configuration_table[i].table, 36);
      mb->flags |= MULTIBOOT2_FLAG_ACPI_RSDP_V2;
    }
  }

  u64 key;
  auto mem_descs = (efi_memory_descriptor*)&mb->efi_mmap[0];
  u64 map_size = sizeof(mb->efi_mmap);
  while (get_memory_map(&map_size, mem_descs, &key,
                       &mb->efi_mmap_descriptor_size,
                       &mb->efi_mmap_descriptor_version) ||
        exit_boot_services((void*)mb->efi_image_handle, key)) {
    // Retry
  }
  mb->flags |= MULTIBOOT2_FLAG_EFI_MMAP;
  mb->efi_mmap_descriptor_count = map_size / mb->efi_mmap_descriptor_size;

  // auto set_virtual_address_map = system_table->runtime_services->set_virtual_address_map;
  // for (int i = 0; i < mb->efi_mmap_descriptor_count; i++) {
  //   auto d = (efi_memory_descriptor*)&mb->efi_mmap[mb->efi_mmap_descriptor_size*i];
  //   d->vaddr = (d->attributes & EFI_MEMORY_RUNTIME) ? d->paddr + KUEFI : 0;
  // }
  // set_virtual_address_map(map_size,
  //                         mb->efi_mmap_descriptor_size,
  //                         mb->efi_mmap_descriptor_version,
  //                         mem_descs);

  // These both normally happen in init32e, but that doesn't run in EFI mode.
  writemsr(0xc0000080, readmsr(0xc0000080) | (1<<0) | (1<<11));
  lcr4(rcr4() | 0x630);  // Set CR4.PAE = CR4.PSE = CR4.OSFXSR = CR4.OSXMMEXCPT = 1.

  volatile struct desctr dtr;
  dtr.limit = sizeof(bootgdt) - 1;
  dtr.base = (u64)bootgdt;
  lgdt((void*)&dtr.limit);

  __asm volatile(
    "call 1f; 1: pop %%rax; sub $(1b-%a0), %%rax;"

    "movq $(physical_address_offset-%a0), %%rbx;"
    "addq %%rax, %%rbx;"
    "movq %%rax, (%%rbx);"

    "movl %%eax, %%ebx;"
    "addl $(kpml4-%a0), %%ebx;"
    "addl %%eax, 0xff0(%%ebx);"
    "addl %%eax, 0xff8(%%ebx);"

    "movl %%eax, %%ebx;"
    "addl $(trampoline_pml4-%a0), %%ebx;"
    "addl %%eax, (%%ebx);"
    "addl %%eax, 0xff0(%%ebx);"
    "addl %%eax, 0xff8(%%ebx);"

    "movl %%eax, %%ebx;"
    "addl $(pdpt0-%a0), %%ebx;"
    "addl %%eax, (%%ebx);"

    "movl %%eax, %%ebx;"
    "addl $(pdptbase-%a0), %%ebx;"
    "addl %%eax, 0x00(%%ebx);"
    "addl %%eax, 0x08(%%ebx);"
    "addl %%eax, 0x10(%%ebx);"
    "addl %%eax, 0x18(%%ebx);"

    "movl %%eax, %%ebx;"
    "addl $(pdptcode-%a0), %%ebx;"
    "addl %%eax, 0xff8(%%ebx);"

    "movl 0xff8(%%ebx), %%ebx;"
    "and $0xfffff000, %%ebx;"
    "mov $0x1000, %%ecx;"
    "1: sub $8, %%ecx;"
    "addl %%eax, (%%ebx,%%ecx);"
    "cmp $0, %%ecx;"
    "jnz 1b;" :: "i"(KCODE) : "rax", "rbx", "rcx");

  __asm volatile(
    "call 1f; 1: pop %%rbx; sub $(1b-%a1), %%rbx;"
    "movabs $(trampoline_pml4-%a1), %%rax;"
    "addq %%rbx, %%rax;"
    "mov %%rax, %%cr3;"

    "add %0, %%rsp;"
    "movabs $1f, %%rax;"
    "jmp *%%rax; 1:"

    "movabs $(kpml4-%a1), %%rax;"
    "addq %%rbx, %%rax;"
    "mov %%rax, %%cr3;"
    "call cmain;"
      :: "r"(KBASE), "i"(KCODE), "rdi"(mbmagic), "rsi"(mbaddr) : "rax", "rbx", "memory");

  panic("cmain should not return?");

  return 1;
}
