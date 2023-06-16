#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>

#define EXT2_BLOCK_SIZE 1024

struct ext2_super_block {
    __u32 s_inodes_count;
    __u32 s_blocks_count;
    __u32 s_r_blocks_count;
    __u32 s_free_blocks_count;
    __u32 s_free_inodes_count;
    __u32 s_first_data_block;
    __u32 s_log_block_size;
    __u32 s_log_frag_size;
    __u32 s_blocks_per_group;
    __u32 s_frags_per_group;
    __u32 s_inodes_per_group;
    __u32 s_mtime;
    __u32 s_wtime;
    __u16 s_mnt_count;
    __u16 s_max_mnt_count;
    __u16 s_magic;
    __u16 s_state;
    __u16 s_errors;
    __u16 s_minor_rev_level;
    __u32 s_lastcheck;
    __u32 s_checkinterval;
    __u32 s_creator_os;
    __u32 s_rev_level;
    __u16 s_def_resuid;
    __u16 s_def_resgid;
    __u32 s_first_ino;
    __u16 s_inode_size;
    __u16 s_block_group_nr;
    __u32 s_feature_compat;
    __u32 s_feature_incompat;
    __u32 s_feature_ro_compat;
    __u8  s_uuid[16];
    char  s_volume_name[16];
    char  s_last_mounted[64];
    __u32 s_algo_bitmap;
    __u8  s_prealloc_blocks;
    __u8  s_prealloc_dir_blocks;
    __u16 s_padding1;
    __u32 s_reserved[204];
};

struct ext2_group_desc {
    __u32 bg_block_bitmap;
    __u32 bg_inode_bitmap;
    __u32 bg_inode_table;
    __u16 bg_free_blocks_count;
    __u16 bg_free_inodes_count;
    __u16 bg_used_dirs_count;
    __u16 bg_pad;
    __u32 bg_reserved[3];
};

struct ext2_inode {
    __u16 i_mode;
    __u16 i_uid;
    __u32 i_size;
    __u32 i_atime;
    __u32 i_ctime;
    __u32 i_mtime;
    __u32 i_dtime;
    __u16 i_gid;
    __u16 i_links_count;
    __u32 i_blocks;
    __u32 i_flags;
    __u32 i_osd1;
    __u32 i_block[15];
    __u32 i_generation;
    __u32 i_file_acl;
    __u32 i_dir_acl;
    __u32 i_faddr;
    __u8  i_osd2[12];
};

struct ext2_dir_entry {
    __u32 inode;
    __u16 rec_len;
    __u8  name_len;
    __u8  file_type;
    char  name[EXT2_NAME_LEN];
};

int ext2_rmdir(const char *path) {
    int ret = 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return -errno;
    }

    struct ext2_super_block sb;
    struct ext2_group_desc gd;
    struct ext2_inode inode;
    struct ext2_dir_entry dir_entry;

    // read superblock
    ret = pread(fd, &sb, sizeof(sb), 1024);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // read group descriptor
    ret = pread(fd, &gd, sizeof(gd), EXT2_BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // read root inode
    ret = pread(fd, &inode, sizeof(inode), EXT2_BLOCK_SIZE * gd.bg_inode_table);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // check if root inode is a directory
    if ((inode.i_mode & 0xF000) != 0x4000) {
        fprintf(stderr, "%s is not a directory\n", path);
        close(fd);
        return -ENOTDIR;
    }

    // find the directory entry for the target directory
    int i;
    for (i = 0; i < 12 && inode.i_block[i]; i++) {
        int offset = 0;
        while (offset < EXT2_BLOCK_SIZE) {
            ret = pread(fd, &dir_entry, sizeof(dir_entry), inode.i_block[i] * EXT2_BLOCK_SIZE + offset);
            if (ret < 0) {
                perror("pread");
                close(fd);
                return -errno;
            }

            if (dir_entry.inode != 0 && strcmp(dir_entry.name, ".") != 0 && strcmp(dir_entry.name, "..") != 0 && strcmp(dir_entry.name, path) == 0) {
                break;
            }

            offset += dir_entry.rec_len;
        }

        if (offset < EXT2_BLOCK_SIZE) {
            break;
        }
    }

    if (i >= 12 || inode.i_block[i] == 0) {
        fprintf(stderr, "failed to find directory entry for %s\n", path);
        close(fd);
        return -ENOENT;
    }

    // read the inode for the target directory
    ret = pread(fd, &inode, sizeof(inode), EXT2_BLOCK_SIZE * gd.bg_inode_table + (dir_entry.inode - 1) * sizeof(inode));
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // check if target inode is a directory
    if ((inode.i_mode & 0xF000) != 0x4000) {
        fprintf(stderr, "%s is not a directory\n", path);
        close(fd);
        return -ENOTDIR;
    }

    // check if target directory is empty
    int offset = 0;
    while (offset < inode.i_size) {
        ret = pread(fd, &dir_entry, sizeof(dir_entry), inode.i_block[0] * EXT2_BLOCK_SIZE + offset);
        if (ret < 0) {
            perror("pread");
            close(fd);
            return -errno;
        }

        if (dir_entry.inode != 0 && strcmp(dir_entry.name, ".") != 0 && strcmp(dir_entry.name, "..") != 0) {
            fprintf(stderr, "%s is not empty\n", path);
            close(fd);
            return -ENOTEMPTY;
        }

        offset += dir_entry.rec_len;
    }

    // clear the inode bitmap
    char bitmap[EXT2_BLOCK_SIZE];
    ret = pread(fd, bitmap, EXT2_BLOCK_SIZE, EXT2_BLOCK_SIZE * gd.bg_inode_bitmap);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    int inode_bit = dir_entry.inode - 1;
    int byte = inode_bit / 8;
    int bit = inode_bit % 8;
    bitmap[byte] &= ~(1 << bit);

    ret = pwrite(fd, bitmap, EXT2_BLOCK_SIZE, EXT2_BLOCK_SIZE * gd.bg_inode_bitmap);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // update the free inode count
    gd.bg_free_inodes_count++;
    ret = pwrite(fd, &gd, sizeof(gd), EXT2_BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // clear the directory entry
    memset(&dir_entry, 0, sizeof(dir_entry));
    ret = pwrite(fd, &dir_entry, sizeof(dir_entry), inode.i_block[i] * EXT2_BLOCK_SIZE + offset - dir_entry.rec_len);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // update the used directory count
    gd.bg_used_dirs_count--;
    ret = pwrite(fd, &gd, sizeof(gd), EXT2_BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    close(fd);
    return 0;
}
