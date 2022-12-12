#include "data.h"
#include "bitmap.h"
#include "blocks.h"
#include <assert.h>
#include "string.h"
#include "dirent.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>

const int inode_block = 1;
const int root_block = 3;

//set up data
void data_init(const char *image_path){

  blocks_init(image_path); 
 
  void *bbm = get_blocks_bitmap();

  bitmap_put(bbm, inode_block, 1);
  bitmap_put(bbm, root_block, 1);


 // (struct entry *) root_entries = blocks_get_block(root_block);
//  root_entries->name = "test.x";

}


//recursively search for a directory entry starting at a given block
struct entry *find_entry_dir(const char *path, int dir_block){
  printf("start finde entry (path, %s), (block: %d)\n", path, dir_block);
  struct entry *list = blocks_get_block(dir_block);
  
  if(strcmp(path, "/") == 0) path = ".";

  if(path[0] == '/') path++;
  printf("search path: (%s)\n", path);
  const char *iter = path;
  while(*iter != '/' && *iter != '\0') iter++;
  int len = (int)(iter - path);


  for(int i = 1; i <= ENTRIES_PER_BLOCK; i++){
    if(bitmap_get(list, i-1) == 1){
      printf("entry search: (len: %d) (path: %s) (name: %s) (i: %d)\n", len, path, (list+i)->name, i-1);
      if(strncmp(path, (list+i)->name, len) == 0){
        struct inode *in = data_inode_ref((list+i)->inode);
        if(*iter == '/' && S_ISDIR(in->mode)){
          //search down another directory
          return find_entry_dir(iter, in->block);
        }

        
        //we've found the end of the path
        if(*iter == '\0') return list + i;
        return NULL;
      }
    }
  }

  return NULL;
}

//find entry for path starting at root
struct entry *find_entry(const char *path){
  return find_entry_dir(path, root_block);
}

//get pointed to inode struct from inode index
struct inode *data_inode_ref(int inode){

  return ((struct inode *)blocks_get_block(inode_block)) + inode;
}


//create a new file
int data_new_file(const char *path, int mode){
  printf("start new file: (path: %s) (mode: %d)\n", path, mode);
  int i = -1;
  void *ibm = get_inode_bitmap();

  if(data_get_inode(path) != -ENOENT){
    return -1;
  }

  if(strcmp(path, ".") != 0){

    while(bitmap_get(ibm, ++i) == 1 && i <= ENTRIES_PER_BLOCK);

    bitmap_put(ibm, i, 1);

    //get inode pointer
    struct inode *in = ((struct inode *)blocks_get_block(inode_block)) + i;

    in->mode = mode;
    in->size = 0;
    in->refs = 1;
    if(strcmp(path, "/") == 0){
      in->block = root_block;
    }else{
      in->block = alloc_block();
    }

    //add a "." entry if we are creating a directory
    if(S_ISDIR(mode)){
      struct entry *e = blocks_get_block(in->block);
      bitmap_put(e, 0, 1);
      strcpy((e+1)->name, ".");
      (e+1)->inode = i;
    }
  
  } else if(strcmp(path, "..") == 0){
    i = -1;
  } else {
    i = data_get_inode("/");
  }

  //dont make entry for "/"
  if(strcmp(path, "/") == 0) return 0;

  char *file = strrchr(path, '/');

  //find the directory table we need to place this file in
  char *dir = malloc(strlen(path));
  strncpy(dir, path, (int)(file-path));
  dir[(int)(file-path)] = '\0';
  if(dir[0] == '\0') strcpy(dir, ".");
  printf("(path: %s) (file: %s) (dir: %s)\n", path, file, dir);
  struct inode *dir_in = data_inode_ref(data_get_inode(dir));
  free(dir);
  struct entry *ent = blocks_get_block(dir_in->block);

  //find free spot in entry table
  int j = 0;
  while(bitmap_get(ent, ++j) == 1);
  bitmap_put(ent, j, 1);
    
  (ent+j+1)->inode = i;
  strncpy((ent+j+1)->name, file+1, 11);
  (ent+j+1)->name[11] = '\0';

  printf("created file (%s)  (dir j: %d) (inode: %d)\n", path, j, i);
  return 0; 

}

//list of file entries in a directory
struct entry *data_list_files(const char *path){
  struct inode *in = data_inode_ref(data_get_inode(path));

  return blocks_get_block(in->block);
}

//find the inode index from a file path
int data_get_inode(const char *path){
  struct entry *en = find_entry(path); 
  
  if(en == NULL) return -ENOENT;

  return en->inode;
}

//get attributes about an inode
int data_attr(int inode, struct stat *st){
  struct inode *in = (struct inode *)blocks_get_block(inode_block) + inode;
  
  st->st_mode = in->mode;
  st->st_size = in->size;
  st->st_uid = getuid();
  st->st_gid = getgid();
  st->st_nlink = in->refs;  
  st->st_ino = inode;

  return 0;
} 

//get a block from a file path for read/write
void *data_get_block(const char *path){
  int inode = data_get_inode(path);
  if(inode == -ENOENT){
    return NULL;
  }

  struct inode *in = (struct inode *)blocks_get_block(inode_block) + inode;
  return blocks_get_block(in->block);

}

//find the directory that lists the given file path
struct entry *find_parent(const char *path){
  char *file = strrchr(path, '/');
  char *dir = malloc(strlen(path));
  strncpy(dir, path, (int)(file-path));
  dir[(int)(file-path)] = '\0';
  if(dir[0] == '\0') strcpy(dir, ".");
  printf("(path: %s) (file: %s) (dir: %s)\n", path, file, dir);
  struct inode *dir_in = data_inode_ref(data_get_inode(dir));
  free(dir);
  struct entry *list = blocks_get_block(dir_in->block);
  return list;
}

//rename/move a directory entry
int data_rename(const char *from, const char *to){
  struct entry *entry = find_entry(from);

  if(entry == NULL) return -ENOENT;
  
  struct entry *parent = find_parent(from);
  int i = (int)(entry-parent) - 1;
  bitmap_put(parent, i, 0);

  //find enclosing directory and create new entry
  struct entry *to_parent = find_parent(to);
  int j = 0;
  while(bitmap_get(to_parent, ++j) == 1);
  bitmap_put(to_parent, j, 1);
  char *file = strrchr(to, '/') + 1;
  strncpy((to_parent + j + 1)->name, file, 12); 
  (to_parent + j + 1)->name[11] = '\0';
  (to_parent + j + 1)->inode = entry->inode;
  return 0;
}

//delete a file
int data_unlink(const char *path){
    
  //find directory
  char *file = strrchr(path, '/');
  char *dir = malloc(strlen(path));
  strncpy(dir, path, (int)(file-path));
  dir[(int)(file-path)] = '\0';
  if(dir[0] == '\0') strcpy(dir, ".");
  printf("(path: %s) (file: %s) (dir: %s)\n", path, file, dir);
  struct inode *dir_in = data_inode_ref(data_get_inode(dir));
  free(dir);
  struct entry *list = blocks_get_block(dir_in->block);


  //pick inode out of entry table
  int inode = -1;

  for(int i = 1; i <= ENTRIES_PER_BLOCK; i++){
    if(bitmap_get(list, i-1) == 1){
      printf("entry search: (path: %s) (name: %s) (i: %d)\n", path, (list+i)->name, i-1);
      if(strncmp(file+1, (list+i)->name, 12) == 0){
        bitmap_put(list, i-1, 0);
        inode = (list+i)->inode;
      }
    }
  }
  
  struct inode *in = (struct inode *)blocks_get_block(inode_block)+inode;
  bitmap_put(get_blocks_bitmap(), in->block, 0);
  bitmap_put(get_inode_bitmap(), inode, 0);
  return 0;
}
