use addr2line::Frame;
use clap::{App, Arg};
use std::env;
use std::collections::HashMap;

const KCODE: u64 = 0xffffffffc0200000;
const KCODEEND: u64 = 0xffffffffc0400000;

fn main() {
    let kernel_path = format!("{}/../../o.qemu/kernel.elf", env!("CARGO_MANIFEST_DIR"));

    let matches = App::new("QStats Viewer")
        .version("0.1")
        .arg(
            Arg::with_name("kernel")
                .short("k")
                .long("kernel")
                .default_value(&kernel_path)
                .takes_value(true),
        )
        .get_matches();

    let kernel = matches.value_of("kernel").unwrap();
    let kernel_file = std::fs::read(kernel).unwrap();
    let object = addr2line::object::File::parse(&kernel_file).unwrap();
    let context = addr2line::Context::new(&object).unwrap();

	let mut filenames = vec!["???".to_string()];
	let mut files = HashMap::new();

	let mut file_indexes = vec![0u32; (KCODEEND - KCODE) as usize];

	let mut max_line = 0;

	let mut distinct = 0;
	let mut last_file = 0;
	let mut last_line = 0;

	for addr in KCODE..KCODEEND {
		if let Ok(Some(addr2line::Location { file: Some(file), line: Some(line), column: _ })) = context.find_location(addr) {
			let filename_index = *files.entry(file.to_string()).or_insert_with(|| {
				filenames.push(file.to_string());
				filenames.len() - 1
			});
			file_indexes[(addr - KCODE) as usize] = filename_index as u32;
			// println!("{}:{}", f, l);

			if last_file != filename_index || last_line != line {
				last_file = filename_index;
				last_line = line;
				distinct += 1;
			}

			if line > max_line {
				max_line = line;
			}
		}
	}

	println!("# filenames = {}, total_bytes = {} KB, max_line = {}, distinct = {}", filenames.len(), filenames.iter().map(|f|f.len()+1).sum::<usize>() >> 10, max_line, distinct);
}
