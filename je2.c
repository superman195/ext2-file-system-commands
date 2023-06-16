#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#include "ext2.h"

unsigned char *disk;

int main(int argc, char **argv) {

    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_mkdir <image file name> <absolute path>\n");
        exit(1);
    }

    int fd = open(argv[1], O_RDWR);

    if(fd == -1) {
        perror("open");
        exit(1);
    }

    // read disk image into memory
    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // get superblock and group descriptor
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);

    // get root inode and block
    struct ext2_inode *root_inode = (struct ext2_inode *)(disk + 1024 * gd->bg_inode_table);
    struct ext2_dir_entry *root_dir = (struct ext2_dir_entry *)(disk + 1024 * root_inode->i_block[0]);

    // traverse path to find parent directory inode and block
    char *path = argv[2];
    char *token = strtok(path, "/");
    struct ext2_inode *parent_inode = root_inode;
    struct ext2_dir_entry *parent_dir = root_dir;

    while(token != NULL) {
        // find directory entry with name matching current token
        int found = 0;
        struct ext2_dir_entry *dir = parent_dir;
        while((unsigned char *)dir < disk + 1024 * (gd->bg_block_bitmap - 1)) {
            if(dir->name_len == strlen(token) && strncmp(token, dir->name, dir->name_len) == 0) {
                found = 1;
                break;
            }
            dir = (void *)dir + dir->rec_len;
        }

        if(!found) {
            fprintf(stderr, "No such file or directory\n");
            exit(ENOENT);
        }

        // update parent inode and block
        parent_inode = (struct ext2_inode *)(disk + 1024 * gd->bg_inode_table + 128 * (dir->inode - 1));
        parent_dir = (struct ext2_dir_entry *)(disk + 1024 * parent_inode->i_block[0]);

        token = strtok(NULL, "/");
    }

    // check if directory already exists
    struct ext2_dir_entry *dir = parent_dir;
    while((unsigned char *)dir < disk + 1024 * (gd->bg_block_bitmap - 1)) {
        if(dir->name_len == strlen(token) && strncmp(token, dir->name, dir->name_len) == 0) {
            fprintf(stderr, "Directory already exists\n");
            exit(EEXIST);
        }
        dir = (void *)dir + dir->rec_len;
    }

    // find free inode and block
    int inode_num = 0;
    int block_num = 0;
    int i;
    for(i = 0; i < sb->s_inodes_count; i++) {
        if(!get_inode_bitmap(disk, gd, i + 1)) {
            inode_num = i + 1;
            set_inode_bitmap(disk, gd, inode_num);
            break;
        }
    }
    if(inode_num == 0) {
        fprintf(stderr, "No free inodes\n");
        exit(ENOSPC);
    }
    for(i = 0; i < sb->s_blocks_count; i++) {
        if(!get_block_bitmap(disk, gd, i + 1)) {
            block_num = i + 1;
            set_block_bitmap(disk, gd, block_num);
            break;
        }
    }
    if(block_num == 0) {
        fprintf(stderr, "No free blocks\n");
        exit(ENOSPC);
    }

    // set up new inode
    struct ext2_inode *new_inode = (struct ext2_inode *)(disk + 1024 * gd->bg_inode_table + 128 * (inode_num - 1));
    new_inode->i_mode = EXT2_S_IFDIR;
    new_inode->i_size = 1024;
    new_inode->i_blocks = 2;
    new_inode->i_links_count = 2;
    new_inode->i_ctime = time(NULL);
    new_inode->i_dtime = 0;
    new_inode->i_gid = 0;
    new_inode->i_uid = 0;
    new_inode->i_generation = 0;
    new_inode->i_flags = 0;
    new_inode->i_block[0] = block_num;
    int j;
    for(j = 1; j < 15; j++) {
        new_inode->i_block[j] = 0;
    }

    // set up new block
    struct ext2_dir_entry *new_dir = (struct ext2_dir_entry *)(disk + 1024 * block_num);
    new_dir->inode = inode_num;
    new_dir->name_len = strlen(token);
    new_dir->rec_len = 1024;
    new_dir->file_type = EXT2_FT_DIR;
    strncpy(new_dir->name, token, new_dir->name_len);
    new_dir->name[new_dir->name_len] = '\0';

    // update parent directory block
    dir = parent_dir;
    while((unsigned char *)dir + dir->rec_len < disk + 1024 * (gd->bg_block_bitmap - 1)) {
        dir = (void *)dir + dir->rec_len;
    }
    int last_entry_size = dir->rec_len;
    int new_entry_size = 8 + new_dir->name_len + (4 - (8 + new_dir->name_len) % 4) % 4;
    if(last_entry_size - new_entry_size < 8) {
        dir->rec_len = last_entry_size - (last_entry_size - 8) % 4;
        dir = (void *)dir + dir->rec_len;
        dir->inode = inode_num;
        dir->name_len = strlen(token);
        dir->rec_len = 1024 - ((unsigned char *)dir - disk) % 1024;
        dir->file_type = EXT2_FT_DIR;
        strncpy(dir->name, token, dir->name_len);
        dir->name[dir->name_len] = '\0';
    } else {
        dir->rec_len = last_entry_size - new_entry_size;
        dir = (void *)dir + dir->rec_len;
        dir->inode = inode_num;
        dir->name_len = strlen(token);
        dir->rec_len = new_entry_size;
        dir->file_type = EXT2_FT_DIR;
        strncpy(dir->name, token, dir->name_len);
        dir->name[dir->name_len] = '\0';
    }

    // update parent inode
    parent_inode->i_links_count++;
    parent_inode->i_ctime = time(NULL);

    return 0;
}
