#![no_std]
#![no_main]
#![feature(abi_efiapi)]
#![feature(asm)]
#![feature(naked_functions)]

extern crate rlibc;

use core::fmt::Write;
use core::{panic::PanicInfo, slice};
use uefi::prelude::*;
use uefi::table::boot::{AllocateType, MemoryType};

struct ST(Option<SystemTable<Boot>>);
unsafe impl Send for ST {}
unsafe impl Sync for ST {}

#[repr(align(8))]
struct Aligned<T>(T);

#[repr(C, align(8))]
struct Mbi {
    size: u32,
    reserved: u32,

    system_table_pointer_type: u32,
    system_table_pointer_size: u32,
    system_table_pointer_pointer: u64,

    bs_not_terminated_type: u32,
    bs_not_terminated_size: u32,

    image_handle_type: u32,
    image_handle_size: u32,
    image_handle_pointer: u64,
}

macro_rules! print {
    ($( $arg:expr ),*) => {
        #[allow(unused_unsafe)]
        unsafe { let _ = write!(SYSTEM_TABLE.0.as_mut().unwrap().stdout(), $( $arg, )* ); }
    }
}

static mut SYSTEM_TABLE: ST = ST(None);

// Placing the payload in the .text section gets it inserted near the start of the binary. This is
// needed for the multiboot headers to work. 
#[link_section = ".text"]
static PAYLOAD: Aligned<[u8; include_bytes!("../../output/ward.elf").len()]> =
    Aligned(*include_bytes!("../../output/ward.elf"));

static mut MBI: Mbi = Mbi {
    size: core::mem::size_of::<Mbi>() as u32,
    reserved: 0,

    system_table_pointer_type: 12,
    system_table_pointer_size: 16,
    system_table_pointer_pointer: 0,

    bs_not_terminated_type: 18,
    bs_not_terminated_size: 8,

    image_handle_type: 20,
    image_handle_size: 16,
    image_handle_pointer: 0,
};

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    print!("Panic '{:?}'\n", info);
    //unsafe { write!(SYSTEM_TALBE.as_mut().unwrap().stderr(), "Panic '{:?}'\n", info); }
    loop {}
}

fn read_u16(bytes: &[u8]) -> u16 {
    u16::from_ne_bytes([bytes[0], bytes[1]])
}
fn read_u32(bytes: &[u8]) -> u32 {
    u32::from_ne_bytes([bytes[0], bytes[1], bytes[2], bytes[3]])
}

#[entry]
fn efi_main(handle: Handle, system_table: SystemTable<Boot>) -> Status {
    unsafe {
        SYSTEM_TABLE = ST(Some(system_table));

        let mut header = None;
        for i in (0..8192).step_by(4) {
            let magic = read_u32(&PAYLOAD.0[i..]);
            if magic == 0xE85250D6 {
                let length = read_u32(&PAYLOAD.0[(i + 8)..]);
                let checksum = read_u32(&PAYLOAD.0[(i + 12)..]);
                if magic.wrapping_add(length).wrapping_add(checksum) == 0 {
                    header = Some((i, length as usize));
                    break;
                }
            }
        }

        if header.is_none() {
            print!("No multiboot header found\n");
            return Status::LOAD_ERROR;
        }
        let (header_offset, header_length) = header.unwrap();

        let mut mb_header_addr = 0;
        let mut mb_load_addr = 0;
        let mut mb_load_end_addr = 0;
        let mut mb_bss_end_addr = 0;
        let mut mb_entry_addr = 0;
        let mut mb_min_addr = 0;
        let mut mb_max_addr = 0;
        let mut mb_align = 0;

        let mut got_addresses_tag = false;
        let mut got_entry_tag = false;
        let mut got_boot_services_tag = false;
        let mut got_relocatable_tag = false;

        let mut offset = header_offset + 16;
        while offset + 8 <= header_offset + header_length {
            let ty = read_u16(&PAYLOAD.0[offset..]);
            let _flags = read_u16(&PAYLOAD.0[(offset + 2)..]);
            let size = read_u32(&PAYLOAD.0[(offset + 4)..]);
            if offset + size as usize > header_offset + header_length {
                break;
            }

            if ty == 2 {
                mb_header_addr = read_u32(&PAYLOAD.0[(offset + 8)..]) as usize;
                mb_load_addr = read_u32(&PAYLOAD.0[(offset + 12)..]) as usize;
                mb_load_end_addr = read_u32(&PAYLOAD.0[(offset + 16)..]) as usize;
                mb_bss_end_addr = read_u32(&PAYLOAD.0[(offset + 20)..]) as usize;
                got_addresses_tag = true;
            } else if ty == 9 {
                mb_entry_addr = read_u32(&PAYLOAD.0[(offset + 8)..]) as usize;
                got_entry_tag = true;
            } else if ty == 7 {
                got_boot_services_tag = true;
            } else if ty == 10 {
                mb_min_addr = read_u32(&PAYLOAD.0[(offset + 8)..]) as usize;
                mb_max_addr = read_u32(&PAYLOAD.0[(offset + 12)..]) as usize;
                mb_align = read_u32(&PAYLOAD.0[(offset + 16)..]) as usize;
                got_relocatable_tag = true;
            } else if ty == 0 {
                break;
            }

            offset += size as usize;
            offset = ((offset - 1) | 7) + 1;
        }

        if !got_addresses_tag {
            print!("Error: missing address tag\n");
            return Status::LOAD_ERROR;
        } else if !got_entry_tag {
            print!("Error: missing entry tag\n");
            return Status::LOAD_ERROR;
        } else if !got_boot_services_tag {
            print!("Error: missing boot services tag\n");
            return Status::LOAD_ERROR;
        } else if !got_relocatable_tag {
            print!("Error: missing relocatable tag\n");
            return Status::LOAD_ERROR;
        }

        if !mb_align.is_power_of_two() {
            print!("Warning: requested alignment isn't a power of two\n");
            mb_align = mb_align.next_power_of_two();
        }
        if mb_align < 0x100000 {
            mb_align = 0x100000;
        }

        let region_base = mb_load_addr & !0xfff;
        let region_end = ((mb_bss_end_addr - 1) | 0xfff) + 1;
        let region_length = region_end - region_base;

        let mut address = (mb_min_addr & !(mb_align - 1)) | (region_base & (mb_align - 1));
        if address < mb_min_addr {
            address += mb_align;
        }

        while let Err(_) = SYSTEM_TABLE
            .0
            .as_mut()
            .unwrap()
            .boot_services()
            .allocate_pages(
                AllocateType::Address(address),
                MemoryType::LOADER_CODE,
                region_length / 4096,
            )
        {
            address += mb_align;
            if address.saturating_add(region_length) > mb_max_addr {
                print!("Unabled to find a suitable load region\n");
                return Status::LOAD_ERROR;
            }
        }

        let kernel = slice::from_raw_parts_mut(address as *mut u8, region_length);
        for b in kernel.iter_mut() {
            *b = 0;
        }

        let src_base = header_offset - (mb_header_addr - mb_load_addr);
        let dst_base = mb_load_addr & (mb_align - 1);
        let copy_len = mb_load_end_addr - mb_load_addr;
        kernel[dst_base..][..copy_len].copy_from_slice(&PAYLOAD.0[src_base..][..copy_len]);

        MBI.image_handle_pointer = core::mem::transmute::<_, u64>(handle);
        MBI.system_table_pointer_pointer =
            core::mem::transmute::<_, u64>(SYSTEM_TABLE.0.take().unwrap());

        let f = (address + mb_entry_addr - region_base) as *const fn(u32, u32);
        let mbi = &MBI as *const _ as u64;
        asm!("push 0; push {0}; ret; 3: jmp 3b", in(reg) f, in("rax") 0x36d76289, in("rbx") mbi, options(noreturn));
    }
}
