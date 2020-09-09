#include "string.h"
#include "amd64.h"
#include "kernel.hh"
#include "uefi.hh"
#include "multiboot.hh"
#include "cmdline.hh"
#include "amd64.h"

void multiboot2_early(u64 mbaddr, multiboot_saved* mb);

extern u64 kpml4[];

extern "C" u64 efi_cmain (u64 mbmagic, u64 mbaddr, void* boot_system_table, void* boot_image_table)
{
  multiboot_saved* mb = (multiboot_saved*)v2p(&multiboot);
  multiboot2_early(mbaddr, mb);
  efi_system_table* system_table = (efi_system_table*)mb->efi_system_table;

  auto get_memory_map = system_table->boot_services->get_memory_map;
  auto exit_boot_services = system_table->boot_services->exit_boot_services;


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

  __asm volatile("movabs $(kpml4-%a1), %%rax; mov %%rax, %%cr3; add %0, %%rsp; movabs $1f, %%rax; jmp *%%rax; 1:"
                 :: "r"(KBASE), "i"(KCODE) : "rax", "memory");
  cmain(mbmagic, mbaddr);
  panic("cmain should not return?");

  return 1;
}
