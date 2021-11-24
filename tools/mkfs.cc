#include "include/types.h"
#include "include/fs.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <dirent.h>

int ninodes = 2400;
int size = 8192*2;

int fsfd;
ward_superblock sb;
char zeroes[BSIZE];
u32 freeblock;
u32 usedblocks;
u32 bitblocks;
u32 freeinode = 1;

void balloc(int);
void wsect(u32, void*);
void winode(u32, ward_dinode*);
void rinode(u32 inum, ward_dinode *ip);
void rsect(u32 sec, void *buf);
u32 ialloc(u16 type);
void iappend(u32 inum, void *p, int n);

// convert to intel byte order
u16
xshort(u16 x)
{
  u16 y;
  u8 *a = (u8*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

u32
xint(u32 x)
{
  u32 y;
  u8 *a = (u8*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

void
copy_files(u16 dir_inode, DIR* dir_dir, int dir_fd)
{
  struct dirent* de;
  while ((de = readdir(dir_dir))) {
    if (de->d_type == DT_REG) {
      int fd = openat(dir_fd, de->d_name, 0);
      if(fd < 0){
        perror(de->d_name);
        exit(1);
      }

      ward_dirent wde;
      u16 inum = ialloc(T_FILE);
      bzero(&wde, sizeof(ward_dirent));
      wde.inum = xshort(inum);
      strncpy(wde.name, de->d_name, DIRSIZ);
      iappend(dir_inode, &wde, sizeof(ward_dirent));

      int cc;
      char buf[BSIZE];
      while((cc = read(fd, buf, sizeof(buf))) > 0)
        iappend(inum, buf, cc);

      close(fd);
    } else if (de->d_type == DT_DIR){
      if(!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
        continue;

      int fd = openat(dir_fd, de->d_name, 0);
      int fd2 = dup(fd);
      if(fd < 0 || fd2 < 0){
        perror(de->d_name);
        exit(1);
      }

      DIR* dir = fdopendir(fd2);

      ward_dirent wde;
      u16 inum = ialloc(T_DIR);
      bzero(&wde, sizeof(ward_dirent));
      wde.inum = xshort(inum);
      strncpy(wde.name, de->d_name, DIRSIZ);
      iappend(dir_inode, &wde, sizeof(ward_dirent));

      bzero(&wde, sizeof(ward_dirent));
      wde.inum = xshort(inum);
      strcpy(wde.name, ".");
      iappend(inum, &wde, sizeof(wde));

      bzero(&wde, sizeof(ward_dirent));
      wde.inum = xshort(dir_inode);
      strcpy(wde.name, "..");
      iappend(inum, &wde, sizeof(wde));

      copy_files(inum, dir, fd);
      closedir(dir);
      close(fd);
    }
  }
}

int
main(int argc, char *argv[])
{
  u32 rootino, off;
  ward_dirent de;
  char buf[BSIZE];
  ward_dinode din;
  int nblocks;

  if(argc < 3){
    fprintf(stderr, "Usage: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(ward_dinode)) == 0);
  assert((BSIZE % sizeof(ward_dirent)) == 0);

  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0){
    perror(argv[1]);
    exit(1);
  }

  bitblocks = (size+BSIZE*8-1)/(BSIZE*8);
  usedblocks = ninodes / IPB + 3 + bitblocks;
  freeblock = usedblocks;

  nblocks = size - usedblocks;

  printf("used %d (bit %d ninode %zu) free %u total %d\n", usedblocks,
         bitblocks, ninodes/IPB + 1, freeblock, nblocks+usedblocks);

  for(size_t i = 0; i < nblocks + usedblocks; i++)
    wsect(i, zeroes);

  sb.size = xint(size);
  sb.nblocks = xint(nblocks); // so whole disk is size sectors
  sb.ninodes = xint(ninodes);

  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  copy_files(rootino, opendir(argv[2]), open(argv[2], 0));

  // fix size of root inode dir
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off/BSIZE) + 1) * BSIZE;
  din.size = xint(off);
  winode(rootino, &din);

  balloc(usedblocks);

  exit(0);
}

void
wsect(u32 sec, void *buf)
{
  if(lseek(fsfd, sec * (long)BSIZE, 0) != sec * (long)BSIZE){
    perror("lseek");
    exit(1);
  }
  if(write(fsfd, buf, BSIZE) != BSIZE){
    perror("write");
    exit(1);
  }
}

u32
i2b(u32 inum)
{
  return (inum / IPB) + 2;
}

void
winode(u32 inum, ward_dinode *ip)
{
  char buf[BSIZE];
  u32 bn;
  ward_dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((ward_dinode*)buf) + (inum % IPB);
  *dip = *ip;
  wsect(bn, buf);
}

void
rinode(u32 inum, ward_dinode *ip)
{
  char buf[BSIZE];
  u32 bn;
  ward_dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((ward_dinode*)buf) + (inum % IPB);
  *ip = *dip;
}

void
rsect(u32 sec, void *buf)
{
  if(lseek(fsfd, sec * (long)BSIZE, 0) != sec * (long)BSIZE){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, BSIZE) != BSIZE){
    perror("read");
    exit(1);
  }
}

u32
ialloc(u16 type)
{
  u32 inum = freeinode++;
  ward_dinode din;

  bzero(&din, sizeof(din));
  din.type = xshort(type);
  din.nlink = xshort(1);
  din.size = xint(0);
  din.gen = 1;
  winode(inum, &din);
  return inum;
}

void
balloc(int used)
{
  u8 buf[BSIZE];
  int bbn = 0;
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  while (used > 0) {
    bzero(buf, BSIZE);
    for(i = 0; i < used && i < BSIZE*8; i++){
      buf[i/8] = buf[i/8] | (0x1 << (i%8));
    }
    printf("balloc: write bitmap block at sector %zu\n", ninodes/IPB + 3 + bbn);
    wsect(ninodes / IPB + 3 + bbn, buf);
    bbn++;
    used -= BSIZE*8;
  }
}

#define min(a, b) ((a) < (b) ? (a) : (b))

void
iappend(u32 inum, void *xp, int n)
{
  char *p = (char*)xp;
  u32 fbn, off, n1;
  ward_dinode din;
  char buf[BSIZE];
  u32 indirect[NINDIRECT];
  u32 x;

  rinode(inum, &din);

  off = xint(din.size);
  while(n > 0){
    fbn = off / BSIZE;
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++);
        usedblocks++;
      }
      x = xint(din.addrs[fbn]);
    } else if (fbn < NDIRECT + NINDIRECT) {
      if(xint(din.addrs[NDIRECT]) == 0){
        // printf("allocate indirect block\n");
        din.addrs[NDIRECT] = xint(freeblock++);
        usedblocks++;
      }
      // printf("read indirect block\n");
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect);
      }
      x = xint(indirect[fbn-NDIRECT]);
    } else {
      int i1 = (fbn - NDIRECT - NINDIRECT) / NINDIRECT;
      int i2 = (fbn - NDIRECT - NINDIRECT) % NINDIRECT;
      int diblock;

      if (xint(din.addrs[NDIRECT+1]) == 0) {
        din.addrs[NDIRECT+1] = xint(freeblock++);
        usedblocks++;
      }

      rsect(xint(din.addrs[NDIRECT+1]), (char*)indirect);
      if (indirect[i1] == 0) {
        indirect[i1] = xint(freeblock++);
        usedblocks++;
        wsect(xint(din.addrs[NDIRECT+1]), (char*)indirect);
      }

      diblock = indirect[i1];
      rsect(xint(diblock), (char*)indirect);
      if (indirect[i2] == 0) {
        indirect[i2] = xint(freeblock++);
        usedblocks++;
        wsect(xint(diblock), (char*)indirect);
      }

      x = xint(indirect[i2]);
    }
    n1 = min((u32)n, (fbn + 1) * BSIZE - off);
    rsect(x, buf);
    bcopy(p, buf + off - (fbn * BSIZE), n1);
    wsect(x, buf);
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off);
  winode(inum, &din);
}
