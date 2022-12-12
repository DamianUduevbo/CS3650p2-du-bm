#include "blocks.h"
#include <assert.h>
#include <sys/stat.h>


#define ENTRIES_PER_BLOCK 100


struct inode {
  int refs;
  int mode;
  int size;
  int block;
};

struct entry {
  char name[12];
  int inode;
};



void data_init(const char *image_path);

int data_new_file(const char *path, int mode);

int data_get_inode(const char *path);

struct inode *data_inode_ref(int inode);

int data_attr(int inode, struct stat *st);

struct entry *data_list_files(const char *path);

void *data_get_block(const char *path);

int data_rename(const char *from, const char *to);

int data_unlink(const char *path);

