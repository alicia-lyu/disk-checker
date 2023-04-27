#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
void write_file_contents_to_output(int fd, struct ext2_inode* inode, FILE* jpg, char* file_contents_buffer, int block_size);
int main(int argc, char **argv) 
{
    if (argc != 3) 
    {
        printf("expected usage: ./runscan inputfile outputfile\n");
        exit(0);
    }

    /* This is some boilerplate code to help you get started, feel free to modify
       as needed! */

    int fd;
    fd = open(argv[1], O_RDONLY);    /* open disk image */

    ext2_read_init(fd);

    struct ext2_super_block super;

    // example read first the super-block and group-descriptor
    read_super_block(fd, &super);
    printf("\n");
    int num_groups = super.s_blocks_count/ super.s_blocks_per_group;
    struct ext2_group_desc* groups = malloc(num_groups * sizeof(struct ext2_group_desc));
    read_group_descs(fd, groups, num_groups);
    // iterate through all block groups
    for (int i = 0; i < num_groups; i++) 
    {
        off_t inode_table_offset = locate_inode_table(i, groups);
        // Iterate through all the inodes in the block group
        for (__u32 j = 0; j < super.s_inodes_per_group; j++) 
        {
            struct ext2_inode inode;
            read_inode(fd, inode_table_offset, j + 1, &inode, super.s_inode_size); // ext 2 inode numbers start at 1
            if (S_ISREG(inode.i_mode))
            {
                if (debug){
                    printf("Found inode %d\n", j + 1);
                    printf("File size: %d\n", inode.i_size);
                } 
                char* buffer = (char*) malloc(block_size);
                // Read the first block of the file into the buffer
                off_t first_block_offset = BLOCK_OFFSET(inode.i_block[0]);
                lseek(fd, first_block_offset, SEEK_SET);
                read(fd, buffer, block_size);     
                // int is_jpg = 0; 
                if (buffer[0] == (char)0xff &&
                    buffer[1] == (char)0xd8 &&
                    buffer[2] == (char)0xff &&
                    (buffer[3] == (char)0xe0 ||
                    buffer[3] == (char)0xe1 ||
                    buffer[3] == (char)0xe8)) 
                {
                    // jpg signature found
                    printf("Inode %d is a jpg\n", j + 1);
                    char filename1[100];
                    sprintf(filename1, "%s/file-%d.jpg", argv[2], j + 1);
                    FILE* jpg = fopen(filename1, "w");
                    // write whole file to the jpg
                    // buffer is one block and we already read the first block
                    // buffer will be reused to read the other blocks
                    // Write data blocks to the jpg file
                    char* indirect_block_buffer = (char*) malloc(block_size);
                    char* file_contents_buffer = (char*) malloc(block_size);
                    write_file_contents_to_output(fd, &inode, jpg, file_contents_buffer, block_size);
                    fclose(jpg);
                    free(indirect_block_buffer);
                    free(file_contents_buffer);
                }
                free(buffer);
            }

        }
    }
    free(groups);
    close(fd);
    return 0;
}

void write_file_contents_to_output(int fd, struct ext2_inode* inode, FILE* jpg, char* file_contents_buffer, int block_size)
{
    // Calculate the number of blocks required to store the file
    unsigned int blocks_required = (inode->i_size + block_size - 1) / block_size; // Round up
    __u32* single_indirect_block = (__u32*) malloc(block_size);
    __u32* double_indirect_block = (__u32*) malloc(block_size);
    
    for (unsigned int k = 0; k < blocks_required; k++) {
        off_t block_offset;

        if (k < 12) { // Direct block pointers
            block_offset = BLOCK_OFFSET(inode->i_block[k]);
        } else if (k < 12 + (block_size / sizeof(__u32))) { // Single indirect block pointers
            if (k == 12) { // Read single indirect block
                lseek(fd, BLOCK_OFFSET(inode->i_block[12]), SEEK_SET);
                read(fd, single_indirect_block, block_size);
            }
            block_offset = BLOCK_OFFSET(single_indirect_block[k - 12]);
        } else if (k < 12 + (block_size / sizeof(__u32)) + (block_size * block_size / (sizeof(__u32) * sizeof(__u32)))) { // Double indirect block pointers
            if (k == 12 + (block_size / sizeof(__u32))) { // Read double indirect block
                lseek(fd, BLOCK_OFFSET(inode->i_block[13]), SEEK_SET);
                read(fd, double_indirect_block, block_size);
            }
            int double_index = (k - 12 - (block_size / sizeof(__u32))) / (block_size / sizeof(__u32));
            int single_index = (k - 12 - (block_size / sizeof(__u32))) % (block_size / sizeof(__u32));

            if (single_index == 0) { // Read single indirect block within double indirect block
                lseek(fd, BLOCK_OFFSET(double_indirect_block[double_index]), SEEK_SET);
                read(fd, single_indirect_block, block_size);
            }
            block_offset = BLOCK_OFFSET(single_indirect_block[single_index]);
        } else {
            fprintf(stderr, "Triple indirect blocks not supported\n");
            exit(1);
        }

        // Read the block into the buffer
        lseek(fd, block_offset, SEEK_SET);
        read(fd, file_contents_buffer, block_size);

        // Write the block to the jpg file
        fwrite(file_contents_buffer, 1, 1024, jpg);
    }
    free(single_indirect_block);
    free(double_indirect_block);
}
