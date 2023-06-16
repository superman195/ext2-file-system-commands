#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ext2fs.h>

#define BLOCK_SIZE 1024

int main(int argc, char *argv[]) {
    int fd, n;
    char buf[BLOCK_SIZE];
    struct ext2_super_block superblock;

    if (argc != 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        exit(1);
    }

    if ((fd = open(argv[1], O_RDONLY)) < 0) {
        perror(argv[1]);
        exit(1);
    }

    // Read the superblock
    lseek(fd, BLOCK_SIZE, SEEK_SET);
    read(fd, &superblock, sizeof(superblock));

    // Print some information about the filesystem
    printf("Block size: %d\n", BLOCK_SIZE << superblock.log_block_size);
    printf("Inodes count: %d\n", superblock.inode_count);

    // Read the first block of the file
    lseek(fd, BLOCK_SIZE * 2, SEEK_SET);
    n = read(fd, buf, BLOCK_SIZE);
    if (n < 0) {
        perror("read");
        exit(1);
    }

    // Print the contents of the block
    printf("First block contents:\n%s\n", buf);

    close(fd);
    return 0;
}
