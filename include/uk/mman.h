// User/kernel shared mmap definitions
#pragma once

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED          0x000001
#define MAP_PRIVATE         0x000002
#define MAP_TYPE            0x00000f    /* Mask for type of mapping */
#define MAP_FIXED           0x000010    /* Interpret addr exactly */
#define MAP_ANONYMOUS       0x000020    /* don't use a file */
#define MAP_POPULATE        0x008000    /* populate (prefault) pagetables */
#define MAP_NONBLOCK        0x010000    /* do not block on IO */
#define MAP_STACK           0x020000    /* give out an address that is best suited for process/thread stacks */
#define MAP_HUGETLB         0x040000    /* create a huge page mapping */
#define MAP_SYNC            0x080000    /* perform synchronous page faults for the mapping */
#define MAP_FIXED_NOREPLACE 0x100000    /* MAP_FIXED which doesn't unmap underlying mapping */
#define MAP_UNINITIALIZED   0x4000000   /* For anonymous mmap, memory could be uninitialized */


#define MAP_FAILED ((void*)-1)

#define MADV_WILLNEED 3

// xv6 extension: invalidate all page tables
#define MADV_INVALIDATE_CACHE 1000
