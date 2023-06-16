#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2fs.h"
#include "help.h"
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

unsigned char *disk;

int ext2_mkdir(int fd, char* path){
    char *dirName = extract_filename(path);
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);  
    if(disk == MAP_FAILED) {  
        perror("mmap");  
        exit(1);  
    }

    //gets superblock and groupd desc.
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
    struct ext2_block_group_descriptor *gd = (struct ext2_block_group_descriptor *)  (disk + 2*BLOCK_SIZE + ((sb -> block_group_nr) *  sizeof(struct ext2_block_group_descriptor)));
    // Checks if file already exists and returns if yes.
	char *directory = strrchr(path, '/');
	directory[(path == directory)? 1:0] = '\0';
    if(file_exists(path, get_block(disk, gd -> inode_table, BLOCK_SIZE), dirName)!=0){
        fprintf(stderr, "mkdir: file exists\n");
        return EEXIST;
    }
    printf("%s", disk);
    //CHECK FOR INODE TO RESERVE
    int free_inode;
    if((free_inode = find_first_free_bit(get_block(disk, gd -> inode_bitmap, BLOCK_SIZE), 0, 128)) == -1){
        fprintf(stderr, "mkdir: operation failed: No free inodes");
        exit(1);
    }
    // printf("aa\n");
    unsigned int dirname_length = strlen(dirName);
    unsigned int dir_entry_size = (8 + dirname_length) + (4 - (8 + dirname_length) % 4) % 4;
    //Creates new directory entry
    struct ext2_dir_entry_2  * new_dir = malloc(dir_entry_size);
	if(!new_dir){
		perror("malloc");
		exit(1);
	}
    new_dir -> inode = free_inode + 1;
    new_dir -> name_len = strlen(dirName);
    new_dir -> rec_len = dir_entry_size;
    new_dir -> file_type = EXT2_FT_DIR;
    // printf("bb\n");
    memcpy(new_dir -> name, dirName, strlen(dirName));
    int new_capacity = gd -> free_block_count;
    
    //Getting info to check that destination is a directory and not reserved
    struct basic_fileinfo destination = find_file(path, get_block(disk, gd -> inode_table, BLOCK_SIZE));
    if(destination.inode < 11 && destination.inode != 2){
        fprintf(stderr, "Invalid destination");
        free(new_dir);
        return ENOENT;
    }//Checks if destination is a directory
    if(destination.type != 'd'){
        fprintf(stderr, "mkdir: destination is not a directory");
        free(new_dir);
        return ENOENT;
   }//Checks if directory has space for the new directory
	if(new_capacity < 1){
		fprintf(stderr, "mkdir: not enough capacity");
		return ENOSPC;
	}
    //Reserves location for new directory entry
    if ((new_capacity = reserve_directory_entry(get_block(disk, gd -> inode_table, BLOCK_SIZE), destination.inode, new_capacity, new_dir, get_block(disk, gd -> block_bitmap, BLOCK_SIZE))) == -1){
        fprintf(stderr, "mkdir: Not enough capacity for file");
        exit(-1);
    }
    //freeing memory and reducing the free inode count
    free(new_dir);
    gd -> free_inode_count --;
    set_inode(get_inode(get_block(disk, gd -> inode_table, BLOCK_SIZE), free_inode + 1),
              free_inode, 0, NULL,
              0,
              get_block(disk, gd -> block_bitmap, BLOCK_SIZE),
              get_block(disk, gd -> inode_bitmap, BLOCK_SIZE),
		EXT2_S_IFDIR);
    gd -> free_block_count = new_capacity;
    printf("Success: Directory entry created.\n");
    //Adds . directory to the new directory entry
	new_dir = malloc(12);
	if (!new_dir){
		perror("malloc");
		exit(1);
	}
    new_dir->inode = free_inode
	new_dir -> inode = free_inode + 1;
	new_dir -> name_len = 1; 
	new_dir -> rec_len = 12;
	new_dir -> file_type = EXT2_FT_DIR;
	new_dir -> name[0] = '.';

	struct ext2_inode *inode = get_inode(get_block(disk, gd -> inode_table, BLOCK_SIZE), free_inode + 1);
	inode -> block[0] = 0;
    // Adds .. to the new directory entry
	new_capacity = reserve_directory_entry(get_block(disk, gd -> inode_table, BLOCK_SIZE), free_inode + 1, new_capacity, new_dir, get_block(disk, gd -> block_bitmap, BLOCK_SIZE));
	new_dir -> inode = destination.inode;
	new_dir -> name_len = 2;
	new_dir -> name[0] = '.';
	new_dir -> name[1] = '.';
	reserve_directory_entry(get_block(disk, gd -> inode_table, BLOCK_SIZE), free_inode + 1, new_capacity, new_dir, get_block(disk, gd -> block_bitmap, BLOCK_SIZE));
	gd -> free_block_count = new_capacity;
	gd -> used_dirs_count += 3;
	free(new_dir);
	free(dirName);
	return 0;
}

int ext2_rm(int fd, char *path){
    disk = mmap(NULL, 128 * BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + BLOCK_SIZE);
    struct ext2_block_group_descriptor *gd = (struct ext2_block_group_descriptor *)  (disk + 2*BLOCK_SIZE + ((sb -> block_group_nr) *  sizeof(struct ext2_block_group_descriptor)));
    
        
    char *dirc, *basec, *dirName, *pth;
    dirc = strdup(path);
    basec = strdup(path);
    pth = dirname(dirc);
    dirName = basename(basec);

    
    // Checks if file already exists and returns if yes.
    if(file_exists(dirc, get_block(disk, gd -> inode_table, BLOCK_SIZE), dirName)==0){
        fprintf(stderr, "rm: File does not exist\n");
        return ENOENT;
    }
    //Getting info to check that destination is a directory and not reserved
    struct basic_fileinfo file_info = find_file(pth, get_block(disk, gd -> inode_table, BLOCK_SIZE));
    if(file_info.inode < 11 && file_info.inode != 2){
        fprintf(stderr, "Invalid destination");
        return ENOENT;
    }
	
	file_info = find_file(path, get_block(disk, gd -> inode_table, BLOCK_SIZE));
	if(file_info.type == 'd'){
        fprintf(stderr, "rm: Cant delete a directory");
        return EISDIR;
    }
    //gets inode of file to be deleted.
    int inode_num = file_info.inode;
    struct ext2_inode *current_file_inode = get_inode(get_block(disk, gd -> inode_table, BLOCK_SIZE), inode_num);
    // Getting parent directory
    struct basic_fileinfo parent_info = find_file(pth, get_block(disk, gd -> inode_table, BLOCK_SIZE));
    inode_num = parent_info.inode;
    char *filename = extract_filename(path);
    char *inode_base = get_block(disk, gd->inode_table, BLOCK_SIZE);
	
    // Loops through the iblocks of parent directory in search for the directory
    // entry to be deleted.
    struct ext2_inode *current_inode = get_inode(inode_base, inode_num);
    unsigned int block_num = current_inode->block[0];
    
    unsigned int cur_index_serviced = 0, cur_inode_index = 0;
    char *current_block = get_block(disk, block_num, BLOCK_SIZE);
    struct ext2_dir_entry_2 *current = (struct ext2_dir_entry_2 *) current_block;
    struct ext2_dir_entry_2 *previous = NULL;
    while(cur_index_serviced < current_inode -> size){
        current = (struct ext2_dir_entry_2 *) (current_block + (cur_index_serviced % BLOCK_SIZE));
        // Checks if current entry is the one to be deleted(name match)
        if (strlen(filename) == current -> name_len && !strncmp(filename, current -> name, strlen(filename))){
		    //If there is no previous entry, two cases exist.
            if(previous==NULL){
				//If the current directory entry takes up the entire block, "free" block by unsetting the corresponding bit in bitmap.
				//and "shift" the block numbers down an index including the indirect blocks.
                if(current->rec_len==BLOCK_SIZE){
                    //unset block and move blocks after by 1.
                    unsigned int cur_index = cur_inode_index;
                    unsigned int blocks_used = (current_inode -> size > 0)? ((current_inode -> size - 1)/BLOCK_SIZE) + 1 : 0;
                    //Loops through all blocks coming up and shifts
					if(cur_inode_index < 12){
						unset_bit(get_block(disk, gd->block_bitmap, BLOCK_SIZE), current_inode -> block[cur_inode_index] - 1);
						gd -> free_block_count++;
					}
					else{
                        //Gets indirect block and unsets bit of corresponding element in that block
						unsigned int *indirect = (unsigned int*) get_block(disk, current_inode -> block[12], BLOCK_SIZE);
						unset_bit(get_block(disk, gd->block_bitmap, BLOCK_SIZE), indirect[cur_inode_index - 12] - 1);
					}
                    //Loops through all blocks coming up
                    while(cur_index < blocks_used-1){
                        unsigned int* indirect_block;
                        //We are on the 11th iblock
                        if(cur_index==11){
                            //Implies 12th block is being used does the shift
							indirect_block = (unsigned int*)get_block(disk, current_inode->block[cur_index+1], BLOCK_SIZE);
                            current_inode->block[cur_index]=indirect_block[0];
							//if indirect block is being used
                            if(cur_index+1 < blocks_used-1){
								unset_bit(get_block(disk, gd->block_bitmap, BLOCK_SIZE), current_inode -> block[12] - 1);
								current_inode -> blocks -= 2;
								//IN THIS CASE FREE THE BLOCK BY UNSETTING THE block[12]TH BIT IN THE BITMAP
                            }
                        }else{
                            //If entry is in indirect block complete the shift within the indirect block.
                            if(cur_index>=12){
                                indirect_block[cur_index - 12] = indirect_block[cur_index - 11];
                            }
							else{
								current_inode->block[cur_index] = current_inode->block[cur_index+1];
							}
                        }
						// Increment curr index
						cur_index++;
                    }
                    current_inode -> size -= BLOCK_SIZE;
                    current_inode -> blocks -= 2;
				}
			    else{
				    //Copy next element in place of current and increment length.
					//Setting previous to next element
					unsigned short current_reclen = current -> rec_len;
					//AND NEXT NOW REPRESENTS NEXT
					struct ext2_dir_entry_2 *next = (struct ext2_dir_entry_2 *) (current_block + ((cur_index_serviced + current -> rec_len) % BLOCK_SIZE));
					memcpy(current, next, next->rec_len); //Note previous is now the next element
					current -> rec_len += current_reclen;
                }
			}else{
                    //Not first
                    previous->rec_len += current->rec_len;
            }
			break;
        }
        //Sets the previous element to current and incremenets cur index
        previous = current;
        cur_index_serviced += current -> rec_len;
        if (cur_index_serviced < current_inode->size && cur_index_serviced % BLOCK_SIZE == 0){
            cur_inode_index++;
            current_block = get_block_from_inode(inode_base, inode_num, cur_inode_index);
            previous=NULL;
        }
    }
    
    //unsets all block bits
    char *bitmaprs = get_block(disk, gd->block_bitmap, BLOCK_SIZE);
    char *bitmapind = get_block(disk, gd->inode_bitmap, BLOCK_SIZE);
    // Decrements i_link_count to make sure it is 0 else we cant
    // unset as there is still a hard link pointing to it.
	current_file_inode -> link_count --;
    // unset inode(unset_bit) if no hard links and unset all block bits(free_inode helper)
	if(current_file_inode -> link_count == 0){
		gd -> free_inode_count++;
		gd -> free_block_count += current_file_inode -> blocks / 2;
		free_inode(current_file_inode, disk, bitmaprs);
		unset_bit(bitmapind, file_info.inode - 1);
	}
	printf("File removed.");
 	return 0;
}

int ext2_rmdir(int fd, char* path) {
    int ret = 0;
    
    struct ext2_super_block sb;
    struct ext2_block_group_descriptor gd;
    struct ext2_inode inode;
    struct ext2_dir_entry_2 dir_entry;

    // read superblock
    ret = pread(fd, &sb, sizeof(sb), 1024);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // read group descriptor
    ret = pread(fd, &gd, sizeof(gd), BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // read root inode
    ret = pread(fd, &inode, sizeof(inode), BLOCK_SIZE * gd.inode_table);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // check if root inode is a directory
    if ((inode.mode & 0xF000) != 0x4000) {
        fprintf(stderr, "%s is not a directory\n", path);
        close(fd);
        return -ENOTDIR;
    }

    // find the directory entry for the target directory
    int i;
    for (i = 0; i < 12 && inode.block[i]; i++) {
        int offset = 0;
        while (offset < BLOCK_SIZE) {
            ret = pread(fd, &dir_entry, sizeof(dir_entry), inode.block[i] * BLOCK_SIZE + offset);
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

        if (offset < BLOCK_SIZE) {
            break;
        }
    }

    if (i >= 12 || inode.block[i] == 0) {
        fprintf(stderr, "failed to find directory entry for %s\n", path);
        close(fd);
        return -ENOENT;
    }

    // read the inode for the target directory
    ret = pread(fd, &inode, sizeof(inode), BLOCK_SIZE * gd.inode_table + (dir_entry.inode - 1) * sizeof(inode));
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    // check if target inode is a directory
    if ((inode.mode & 0xF000) != 0x4000) {
        fprintf(stderr, "%s is not a directory\n", path);
        close(fd);
        return -ENOTDIR;
    }

    // check if target directory is empty
    int offset = 0;
    while (offset < inode.size) {
        ret = pread(fd, &dir_entry, sizeof(dir_entry), inode.block[0] * BLOCK_SIZE + offset);
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
    char bitmap[BLOCK_SIZE];
    ret = pread(fd, bitmap, BLOCK_SIZE, BLOCK_SIZE * gd.inode_bitmap);
    if (ret < 0) {
        perror("pread");
        close(fd);
        return -errno;
    }

    int inode_bit = dir_entry.inode - 1;
    int byte = inode_bit / 8;
    int bit = inode_bit % 8;
    bitmap[byte] &= ~(1 << bit);

    ret = pwrite(fd, bitmap, BLOCK_SIZE, BLOCK_SIZE * gd.inode_bitmap);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // update the free inode count
    gd.free_inode_count++;
    ret = pwrite(fd, &gd, sizeof(gd), BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // clear the directory entry
    memset(&dir_entry, 0, sizeof(dir_entry));
    ret = pwrite(fd, &dir_entry, sizeof(dir_entry), inode.block[i] * BLOCK_SIZE + offset - dir_entry.rec_len);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    // update the used directory count
    gd.used_dirs_count--;
    ret = pwrite(fd, &gd, sizeof(gd), BLOCK_SIZE * 2);
    if (ret < 0) {
        perror("pwrite");
        close(fd);
        return -errno;
    }

    close(fd);
    return 0;
}

int main(int argc, char **argv) {

    //Checks valid no of argument
    if(argc != 4) {
        fprintf(stderr, "Usage: ./je2fs  <image file name> <command> <file path on ext2 image>\n");
        exit(1);
    }
    //Gets arguments
    int fd = open(argv[1], O_RDWR);
    char *path = argv[3];
    
    if(strcmp(argv[2],"mkdir") == 0) {
    
    	ext2_mkdir(fd, path);
    }
    if(strcmp(argv[2],"rm") == 0) {
    	ext2_rm(fd, path);
    }
    if(strcmp(argv[2],"rmdir") == 0) {
    	ext2_rmdir(fd, path);
    }
    //maps disk in memory and checks for error
    
	return 0;
}
