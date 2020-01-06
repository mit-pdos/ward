#include "types.h"

struct efi_simple_text_output_protocol {
  void* reset;
  u64 (__attribute__((ms_abi)) *output_string) (efi_simple_text_output_protocol*, u16*);
};

struct efi_memory_descriptor;

typedef u64 (__attribute__((ms_abi)) *EFI_GET_MEMORY_MAP)(u64* map_size, efi_memory_descriptor* map, u64* key, u64* desc_size, u32* desc_version);
typedef u64 (__attribute__((ms_abi)) *EFI_EXIT)(void*, u64, u64, u16*);
typedef u64 (__attribute__((ms_abi)) *EFI_EXIT_BOOT_SERVICES)(void*, u64);

typedef u64 (__attribute__((ms_abi)) *EFI_SET_VIRTUAL_ADDRESS_MAP)(u64, u64, u32, efi_memory_descriptor*);

struct efi_boot_services {
  struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc;
    u32 reserved;
  } header;

  void* raise_tpl;
  void* restore_tpl;

  void* alloc_pages;
  void* free_pages;
  EFI_GET_MEMORY_MAP get_memory_map;
  void* alloc_pool;
  void* free_pool;

  void* create_event;
  void* set_timer;
  void* wait_for_event;
  void* signal_event;
  void* close_event;
  void* check_event;

  void* install_prot;
  void* reinstall_prot;
  void* uninstall_prot;
  void* handle_prot;
  void* _reserved1;
  void* register_prot_notify;
  void* locate_handle;
  void* locate_dev_path;
  void* install_config_table;

  void* image_load;
  void* image_start;
  void* exit;
  void* image_unload;

  EFI_EXIT_BOOT_SERVICES exit_boot_services;
};

struct efi_runtime_services {
  struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc;
    u32 reserved;
  } header;
  void* get_time;
  void* set_time;
  void* get_wakeup_time;
  void* set_wakeup_time;

  EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map;
  void* convert_pointer;

  void* get_variable;
  void* get_next_variable;
  void* set_variable;

  void* get_next_high_monotonic_count;
  void* reset_system;
};

struct efi_system_table {
  struct {
    u64 signature;
    u32 revision;
    u32 header_size;
    u32 crc;
    u32 reserved;
  } header;
  u16* firmware_vendor;
  u32 firmware_revision;
  void* console_in_handle;
  void* console_in_prot;
  void* console_out_handle;
  efi_simple_text_output_protocol* console_out_prot;
  void* console_err_handle;
  void* console_err_prot;
  void* runtime_services;
  void* boot_services;
  u64 num_table_entries;
  void* configuration_table;
};

struct efi_memory_descriptor {
  u32 type;
  u64 paddr;
  u64 vaddr;
  u64 npages;
  u64 attributes;
};
