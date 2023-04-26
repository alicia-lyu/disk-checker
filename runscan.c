#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>


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
                    // is_jpg = 1;
                    printf("Inode %d is a jpg\n", j + 1);
                }
            }

        }
    }

    close(fd);
    return 0;
}