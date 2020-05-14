#pragma once

// xv6-specific file type codes
#define T_DIR  004   // Directory
#define T_FILE 010   // File
#define T_DEV  006   // Special device
#define T_FIFO 001   // Pipe

struct iovec {
  void* base;
  size_t len;
};
