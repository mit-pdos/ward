set architecture i386:x86-64
target remote localhost:1234
symbol-file output/ward.elf
#hb panic
#hb kerneltrap

source tools/xv6-gdb.py
