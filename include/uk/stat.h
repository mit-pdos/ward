// User/kernel shared stat definitions
#pragma once

#include <uk/fs.h>
#include <uk/time.h>

struct kernel_stat {
  dev_t st_dev;                 /* Device number */
  ino_t st_ino;                 /* Inode number on device */
  nlink_t st_nlink;             /* Number of links to file */
  mode_t st_mode;               /* Mode (including file type) */
  unsigned int st_uid;          /* User ID of the file's owner.        */
  unsigned int st_gid;          /* Group ID of the file's group.*/
  int __pad0;
  dev_t st_rdev;                /* Device number, if device.  */
  off_t st_size;                /* Size of file, in bytes.  */
  long st_blksize;              /* Optimal block size for I/O.  */
  long st_blocks;               /* Nr. 512-byte blocks allocated.  */
  struct timespec st_atim;      /* Time of last access.  */
  struct timespec st_mtim;      /* Time of last modification.  */
  struct timespec st_ctim;      /* Time of last status change.  */
};

#define S_IFMT 00170000
#define __S_IFMT_SHIFT 12
