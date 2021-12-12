#![no_std]
#![no_main]
#![feature(abi_efiapi)]
#![feature(asm)]

extern crate rlibc;

use core::cell::UnsafeCell;
use core::fmt::Write;
use core::{panic::PanicInfo, slice};
use uefi::prelude::*;
use uefi::proto::console::gop::GraphicsOutput;
use uefi::proto::console::gop::PixelFormat;
use uefi::table::boot::{AllocateType, MemoryType};

struct ST(Option<SystemTable<Boot>>);
unsafe impl Send for ST {}
unsafe impl Sync for ST {}

#[repr(align(8))]
struct Aligned<T>(T);

#[repr(C, align(8))]
#[derive(Default)]
struct FramebufferTag {
    ty: u32,
    size: u32,
    framebuffer_addr: u64,
    framebuffer_pitch: u32,
    framebuffer_width: u32,
    framebuffer_height: u32,
    framebuffer_bpp: u8,
    framebuffer_type: u8,
    framebuffer_reserved: u8,
    framebuffer_red_field_position: u8,
    framebuffer_red_mask_size: u8,
    framebuffer_green_field_position: u8,
    framebuffer_green_mask_size: u8,
    framebuffer_blue_field_position: u8,
    framebuffer_blue_mask_size: u8,
}

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

    framebuffer: FramebufferTag,
}

macro_rules! print {
    ($( $arg:expr ),*) => {
        {
            #[allow(unused_unsafe)]
            unsafe { let _ = write!(SYSTEM_TABLE.0.as_mut().unwrap().stdout(), $( $arg, )* ); }
        }
    }
}

static mut SYSTEM_TABLE: ST = ST(None);

#[used]
#[link_section = ".data"]
static mut DUMMY: u64 = 0;

// Placing the payload in the .text section gets it inserted near the start of the binary. This is
// needed for the multiboot headers to work.
#[link_section = ".text"]
static PAYLOAD: Aligned<[u8; include_bytes!("../../output/kernel-stripped.elf").len()]> =
    Aligned(*include_bytes!("../../output/kernel-stripped.elf"));

static mut MBI: Mbi = Mbi {
    size: (core::mem::size_of::<Mbi>() - core::mem::size_of::<FramebufferTag>()) as u32,
    reserved: 0,

    system_table_pointer_type: 12,
    system_table_pointer_size: 16,
    system_table_pointer_pointer: 0,

    bs_not_terminated_type: 18,
    bs_not_terminated_size: 8,

    image_handle_type: 20,
    image_handle_size: 16,
    image_handle_pointer: 0,

    framebuffer: FramebufferTag {
        ty: 8,
        size: core::mem::size_of::<FramebufferTag>() as u32,
        framebuffer_addr: 0,
        framebuffer_pitch: 0,
        framebuffer_width: 0,
        framebuffer_height: 0,
        framebuffer_bpp: 32,
        framebuffer_type: 1,
        framebuffer_reserved: 0,
        framebuffer_red_field_position: 0,
        framebuffer_red_mask_size: 8,
        framebuffer_green_field_position: 8,
        framebuffer_green_mask_size: 8,
        framebuffer_blue_field_position: 16,
        framebuffer_blue_mask_size: 8,
    },
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
        MBI.system_table_pointer_pointer = *core::mem::transmute::<_, &u64>(&system_table);
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

        print!("Zeroing memory\n");
        let kernel = slice::from_raw_parts_mut(address as *mut u8, region_length);
        for b in kernel.iter_mut() {
            *b = 0;
        }

        print!(
            "Loading kernel at address=0x{:x} length=0x{:x}\n",
            address, region_length
        );
        let src_base = header_offset - (mb_header_addr - mb_load_addr);
        let dst_base = mb_load_addr & (mb_align - 1);
        let copy_len = mb_load_end_addr - mb_load_addr;
        kernel[dst_base..][..copy_len].copy_from_slice(&PAYLOAD.0[src_base..][..copy_len]);

        let f = address + mb_entry_addr - region_base;
        let mbi = &MBI as *const _ as u64;
        MBI.image_handle_pointer = core::mem::transmute::<_, u64>(handle);

        if let Ok(ref mut gop) = SYSTEM_TABLE
            .0
            .as_mut()
            .unwrap()
            .boot_services()
            .locate_protocol::<GraphicsOutput>()
        {
            if gop.status() == Status::SUCCESS {
                print!("Configuring framebuffer\n");

                let gop: &UnsafeCell<_> = gop.log();
                let gop = &mut *UnsafeCell::get(gop);

                let _mode = gop.current_mode_info();
                if gop.current_mode_info().pixel_format() != PixelFormat::Rgb {
                    let mut chosen: Option<uefi::proto::console::gop::Mode> = None;
                    for m in gop
                        .modes()
                        .filter(|m| m.status() == Status::SUCCESS)
                        .map(|m| m.log())
                    {
                        if m.info().pixel_format() != PixelFormat::Rgb && m.info().pixel_format() != PixelFormat::Bgr {
                            continue;
                        }

                        /*if m.info().resolution() == mode.resolution() {
                            chosen = Some(m);
                            break;
                        } else*/ if chosen.is_none()
                            || m.info().resolution().1
                                > chosen.as_ref().unwrap().info().resolution().1
                        {
                            chosen = Some(m);
                        }
                    }
                    if let Some(chosen) = chosen {
                        let _ = gop.set_mode(&chosen);
                    }
                }

                let mode = gop.current_mode_info();
                if mode.pixel_format() == PixelFormat::Rgb || mode.pixel_format() == PixelFormat::Bgr {
                    MBI.size += core::mem::size_of::<FramebufferTag>() as u32;
                    MBI.framebuffer.framebuffer_addr = gop.frame_buffer().as_mut_ptr() as u64;
                    MBI.framebuffer.framebuffer_pitch = mode.stride() as u32 * 4;
                    MBI.framebuffer.framebuffer_width = mode.resolution().0 as u32;
                    MBI.framebuffer.framebuffer_height = mode.resolution().1 as u32;

                    if mode.pixel_format() == PixelFormat::Bgr {
                        MBI.framebuffer.framebuffer_red_field_position = 16;
                        MBI.framebuffer.framebuffer_blue_field_position = 0;
                    }
                }
            }
        }

        //print!("Booting kernel entry=0x{:x}\n", f as u64);

        asm!("or rsp, 0xf; sub rsp, 0xf; push {0}; mov rbx, {1}; ret; 3: jmp 3b", in(reg) f, in(reg) mbi, in("rax") 0x36d76289, options(noreturn));
    }
}
