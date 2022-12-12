// based on cs3650 starter code

#include <assert.h>
#include <bsd/string.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "data.h"
#include "blocks.h"
#include "bitmap.h"

#define FUSE_USE_VERSION 26
#include <fuse.h>

// implementation for: man 2 access
// Checks if a file exists.
int nufs_access(const char *path, int mask) {

  int rv = data_get_inode(path);

  rv = (rv == -ENOENT) ? -ENOENT : 0;


  printf("access(%s, %04o) -> %d\n", path, mask, rv);
  
  return rv;

/*
  int rv = 0;

  // Only the root directory and our simulated file are accessible for now...
  if (strcmp(path, "/") == 0 || strcmp(path, "/hello.txt") == 0) {
    rv = 0;
  } else { // ...others do not exist
    rv = -ENOENT;
  }

  printf("access(%s, %04o) -> %d\n", path, mask, rv);
  return rv;
*/
}

// Gets an object's attributes (type, permissions, size, etc).
// Implementation for: man 2 stat
// This is a crucial function.
int nufs_getattr(const char *path, struct stat *st) {
  int inode = data_get_inode(path);

  int rv = 0;
  if(inode == -ENOENT){
    rv = -ENOENT;
  }

  data_attr(inode, st);

  printf("getattr(%s) -> (%d) {mode: %04o, size: %ld} (inode: %d)\n", path, rv, st->st_mode, st->st_size, inode);

  return rv;
/*

  int rv = 0;

  // Return some metadata for the root directory...
  if (strcmp(path, "/") == 0) {
    st->st_mode = 040755; // directory
    st->st_size = 0;
    st->st_uid = getuid();
  }
  // ...and the simulated file...
  else if (strcmp(path, "/hello.txt") == 0) {
    st->st_mode = 0100644; // regular file
    st->st_size = 6;
    st->st_uid = getuid();
  } else { // ...other files do not exist on this filesystem
    rv = -ENOENT;
  }
  printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, rv, st->st_mode,
         st->st_size);
  return rv;
  */
}

// implementation for: man 2 readdir
// lists the contents of a directory
int nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
  
  struct stat st;
  struct entry *files = data_list_files(path);

  //call filler() on all the entries that arent empty
  for(int i = 1; i <= ENTRIES_PER_BLOCK; i++){
    if(bitmap_get(files, i-1) == 1 && strcmp((files+i)->name, "/") != 0){
      data_attr((files + i)->inode, &st);
      char *name = (files + i)->name;
      if(name[0] == '/') name++;
      filler(buf, name, &st, 0);
      printf("dir entry (%d): (name: %s, inode: %d)\n", i-1, name, (files+i)->inode);
    }
  }

  printf("readdir(%s) -> %d\n", path, 0);
  return 0;
 
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
// Note, for this assignment, you can alternatively implement the create
// function.
int nufs_mknod(const char *path, mode_t mode, dev_t rdev) {
  int rv = data_new_file(path, mode);
  printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int nufs_mkdir(const char *path, mode_t mode) {
  int rv = nufs_mknod(path, mode | 040000, 0);
  printf("mkdir(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_unlink(const char *path) {
  int rv = data_unlink(path);
  printf("unlink(%s) -> %d\n", path, rv);
  return rv;
}

int nufs_link(const char *from, const char *to) {
  int rv = -1;
  printf("link(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_rmdir(const char *path) {
  int rv = -1;

  printf("rmdir(%s) -> %d\n", path, rv);
  return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int nufs_rename(const char *from, const char *to) {
  int rv = data_rename(from, to);

  printf("rename(%s => %s) -> %d\n", from, to, rv);
  return rv;
}

int nufs_chmod(const char *path, mode_t mode) {
  int rv = -1;
  printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
  return rv;
}

int nufs_truncate(const char *path, off_t size) {
  int rv = 0;

  int inode = data_get_inode(path);
  data_inode_ref(inode)->size = size;

  printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
  return rv;
}

// This is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
// You can just check whether the file is accessible.
int nufs_open(const char *path, struct fuse_file_info *fi) {
  int rv = 0;
  printf("open(%s) -> %d\n", path, rv);
  return rv;
}

// Actually read data
int nufs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
  int rv = size;

  int inode = data_get_inode(path);
  struct inode *in = data_inode_ref(inode);  

  void *block = data_get_block(path);
  char *offset_ptr = ((char *)block) + offset; 

  //dont copy past end of file
  if (size + offset <= in->size) {
    memcpy(buf, offset_ptr, size);
  } else {
    rv = in->size - offset;
    memcpy(buf, offset_ptr, in->size - offset);
  }

  printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Actually write data
// copy from buffer to block
int nufs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {
  
  assert(size != 0);

  int rv = size;
  
  int inode = data_get_inode(path);
  struct inode *in = data_inode_ref(inode);

  void *block = data_get_block(path);
  char *offset_ptr = ((char *)block) + offset;

  //dont expand file past end of block
  if (size + offset <= BLOCK_SIZE) {
    memcpy(offset_ptr, buf, size);
    if(in->size < offset + size)
      in->size = offset + size;
  } else {
    rv = BLOCK_SIZE - offset;
    memcpy(offset_ptr, buf, BLOCK_SIZE - offset);
    in->size = BLOCK_SIZE;
  }
  
  printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
  return rv;
}

// Update the timestamps on a file or directory.
int nufs_utimens(const char *path, const struct timespec ts[2]) {
  int rv = -1;
  printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n", path, ts[0].tv_sec,
         ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
  return rv;
}

// Extended operations
int nufs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
               unsigned int flags, void *data) {
  int rv = -1;
  printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
  return rv;
}

void nufs_init_ops(struct fuse_operations *ops) {
  memset(ops, 0, sizeof(struct fuse_operations));
  ops->access = nufs_access;
  ops->getattr = nufs_getattr;
  ops->readdir = nufs_readdir;
  ops->mknod = nufs_mknod;
  // ops->create   = nufs_create; // alternative to mknod
  ops->mkdir = nufs_mkdir;
  ops->link = nufs_link;
  ops->unlink = nufs_unlink;
  ops->rmdir = nufs_rmdir;
  ops->rename = nufs_rename;
  ops->chmod = nufs_chmod;
  ops->truncate = nufs_truncate;
  ops->open = nufs_open;
  ops->read = nufs_read;
  ops->write = nufs_write;
  ops->utimens = nufs_utimens;
  ops->ioctl = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int main(int argc, char *argv[]) {
  assert(argc > 2 && argc < 6);
  printf("TODO: mount %s as data file\n", argv[--argc]);
  data_init(argv[argc]);
  if(data_get_inode("/") == -ENOENT){
    printf("/ not found!\n");
    data_new_file("/", 040755);
   // data_new_file(".", 040755);
  //  data_new_file("..", 040755);
  }
  // storage_init(argv[--argc]);
  nufs_init_ops(&nufs_ops);
  return fuse_main(argc, argv, &nufs_ops, NULL);
}
