#include "amd64.h"
#include "kernel.hh"
#include "uefi.hh"
#include "multiboot.hh"

void initvga(void);
void initmultiboot(u64 mbmagic, u64 mbaddr);


extern "C" u64 efi_cmain (u64 mbmagic, u64 mbaddr)
{
  // Copy lowest PTE from uefi page table to kpml4, then switch to it. This
  // creates an identity mapping for the first 512 GB of memory.
  extern u64 kpml4[];
  u64* kpml4_cr3 = (u64*)v2p(kpml4);
  u64* uefi_cr3 = (u64*)rcr3();
  u64 kpml4_pte = kpml4_cr3[0];
  kpml4_cr3[0] = uefi_cr3[0];
  lcr3((u64)kpml4_cr3);

  initmultiboot(mbmagic, mbaddr);
  initvga();

  cprintf("Booting in UEFI mode...\n");

  auto system_table = (efi_system_table*)p2v(multiboot.efi_system_table);
  auto boot_services = (efi_boot_services*)p2v((u64)system_table->boot_services);
  EFI_GET_MEMORY_MAP get_memory_map = boot_services->get_memory_map;
  EFI_EXIT_BOOT_SERVICES exit_boot_services = boot_services->exit_boot_services;

  u64 map = v2p(multiboot.efi_mmap);
  u64 map_size = sizeof(multiboot.efi_mmap);
  u64 key, ret;
  while (ret) {
    ret = get_memory_map(&map_size, (efi_memory_descriptor*)map, &key,
                         &multiboot.efi_mmap_descriptor_size,
                         &multiboot.efi_mmap_descriptor_version);
    if (ret) {
      cprintf("ERROR: get_memory_map returned %lx\n", ret);
      continue;
    }

    multiboot.efi_mmap_descriptor_count = map_size / multiboot.efi_mmap_descriptor_size;
    multiboot.flags |= MULTIBOOT2_FLAG_EFI_MMAP;

    ret = exit_boot_services((void*)multiboot.efi_image_handle, key);
    if (ret) {
      cprintf("ERROR: exit_boot_services returned %lx\n", ret);
    }

    break;
  }

  cprintf("Exited boot services\n");

  for (int i = 0; i < multiboot.efi_mmap_descriptor_count; i++) {
    auto d = (efi_memory_descriptor*)&multiboot.efi_mmap[multiboot.efi_mmap_descriptor_size*i];

    // TODO: d->type = 5 means EFI runtime code. This must be mapped executable,
    // probably somewhere other than the direct map.
    if (d->attributes & 0x8000000000000000UL) {
      d->vaddr = (u64)p2v(d->paddr);
    }
  }

  cprintf("system_table->runtime_services = %p\n", system_table->runtime_services);
  auto runtime_services = (efi_runtime_services*)p2v((u64)system_table->runtime_services);
  EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map = runtime_services->set_virtual_address_map;
  ret = set_virtual_address_map(map_size, multiboot.efi_mmap_descriptor_size,
                                multiboot.efi_mmap_descriptor_version, (efi_memory_descriptor*)map);
  cprintf("Set virtual address map (ret = %ld)\n", ret);
  cprintf("system_table->runtime_services = %p\n", system_table->runtime_services);

  // Restore lowest PTE in kpml4 so other code doesn't get confused.
  kpml4_cr3[0] = kpml4_pte;

  cprintf("About to call cmain(%lx, %lx)\n", mbmagic, mbaddr);
  cmain(mbmagic, mbaddr);
  panic("cmain should not return?");

  return 1;
}
