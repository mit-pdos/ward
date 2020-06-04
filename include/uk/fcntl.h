// User/kernel shared file control definitions
#pragma once

#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR      00000002
#define O_CREAT     00000100
#define O_EXCL      00000200
#define O_NOCTTY    00000400
#define O_TRUNC     00001000
#define O_APPEND    00002000
#define O_NONBLOCK  00004000
#define O_CLOEXEC   02000000
#define O_NDELAY    O_NONBLOCK

// #define O_WAIT    0x800 // (xv6) open waits for create, read for write
#define O_ANYFD   00010000 // (xv6) no need for lowest FD
// #define AT_FDCWD  -100
