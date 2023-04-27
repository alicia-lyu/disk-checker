#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

void write_file_contents_to_output(int fd, struct ext2_inode** inode_ref, FILE* file_copy, char* file_contents_buffer);
int create_dir(char* dirname);
int is_jpg(struct ext2_inode inode, int img_fd);
void search_for_filename(int inode_number, char* original_filename);
int copy_file_with_new_name(char* path, int img_fd, struct ext2_inode* inode);

int create_dir(char* dirname)
{
    // Create directory argv[2] if not exists using mkdir, detect existence using opendir
    DIR* dir = opendir(dirname);
    if (dir) {
        closedir(dir);
        printf("Output dir already exists\n");
        return 1;
    } else if (errno == ENOENT) {
        mkdir(dirname, 0777);
        return 0;
    } else {
        printf("Error creating directory\n");
        return 1;
    }
}
// To-Do revise all the code changes since last commit
struct jpg_file {
    int inode_number;
    char* filename;
};

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

    char* dirname = argv[2];
    int create_dir_ret = create_dir(dirname);
    if (create_dir_ret) exit(1);

    ext2_read_init(fd);

    struct ext2_super_block super;

    // example read first the super-block and group-descriptor
    read_super_block(fd, &super);
    printf("\n");
    int num_groups = super.s_blocks_count/ super.s_blocks_per_group;
    struct ext2_group_desc* groups = malloc(num_groups * sizeof(struct ext2_group_desc));
    read_group_descs(fd, groups, num_groups);

    int num_jpg = 0;
    // get inodes of all jpgs
    for (int i = 0; i < num_groups; i++) 
    {
        off_t inode_table_offset = locate_inode_table(i, groups);
        // Iterate through all the inodes in the block group
        for (__u32 j = 0; j < super.s_inodes_per_group; j++) 
        {
            struct ext2_inode inode;
            read_inode(fd, inode_table_offset, j + 1, &inode, super.s_inode_size); // ext 2 inode numbers start at 1
            if (S_ISREG(inode.i_mode)) {
                if (debug){
                    printf("Found inode %d\n", j + 1);
                    printf("File size: %d\n", inode.i_size);
                }
                // Inspect if file is jpg
                int is_jpg_flag = is_jpg(inode, fd);
                if (!is_jpg_flag) continue;
                if (debug) printf("Inode %d is a jpg\n", j + 1);
                num_jpg++;
            }
        }
    }
    printf("Total number of jpgs found: %d\n", num_jpg);
    struct jpg_file* jpg_files = (struct jpg_file*) malloc(num_jpg * sizeof(struct jpg_file));
    
    // add filenames to jpg_files
    for (int i = 0; i < num_groups; i++) 
    {
        off_t inode_table_offset = locate_inode_table(i, groups);
        // Iterate through all the inodes in the block group
        for (__u32 j = 0; j < super.s_inodes_per_group; j++) 
        {
            struct ext2_inode inode;
            read_inode(fd, inode_table_offset, j + 1, &inode, super.s_inode_size); // ext 2 inode numbers start at 1
            if (S_ISDIR(inode.i_mode)) {
                // iterate through all directory entries in data block
                struct ext2_dir_entry_2* dir_entries = (struct ext2_dir_entry_2*) malloc(block_size);
                off_t data_block_offset = locate_data_blocks(i, groups);
                lseek(fd, data_block_offset + BLOCK_OFFSET(inode.i_block[0]), SEEK_SET);
                read(fd, dir_entries, block_size);
                off_t offset = 0;
                while (offset < inode.i_size) {
                    struct ext2_dir_entry_2 dir_entry = dir_entries[offset];
                    char* filename = (char*) malloc(dir_entry.name_len + 1);
                    memcpy(filename, dir_entry.name, dir_entry.name_len);
                    filename[dir_entry.name_len] = '\0';
                    if (debug) printf("Found directory entry: %s\n", filename);
                    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
                        offset += dir_entry.name_len + 8;
                        continue;
                    }
                    char* original_filename = dir_entry.name;
                    for (int i = 0; i< num_jpg; i++) {
                        struct jpg_file jpg_file = jpg_files[i];
                        if (dir_entry.inode == jpg_file.inode_number) {
                            jpg_file.filename = original_filename;
                            break;
                        }
                    }
                    if (debug) printf("Found original filename: %s\n", original_filename);
                    offset += dir_entry.name_len + 8;
                }

            }
        }
    }
    // copy jpgs to output directory with different filenames
    for (int i = 0; i< num_jpg; i++) {
        struct jpg_file jpg_file = jpg_files[i];
        char path_inode[100];
        sprintf(path_inode, "%s/file-%d.jpg", argv[2], j+1);
        int copy_ret = copy_file_with_new_name(path_inode, fd, &inode);
        if (copy_ret) exit(1);
    }
    free(groups);
    close(fd);
    return 0;
}

int copy_file_with_new_name(char* path, int img_fd, struct ext2_inode* inode)
{
    FILE* file_copy = fopen(path, "w");
    if (file_copy == NULL) {
        printf("Error creating file\n");
        return 1;
    }
    char* file_contents_buffer = (char*) malloc(block_size);
    write_file_contents_to_output(img_fd, &inode, file_copy, file_contents_buffer);
    // write file details
    fclose(file_copy);
    free(file_contents_buffer);
    return 0;
}

int is_jpg(struct ext2_inode inode, int img_fd) 
{
    char* buffer = (char*) malloc(block_size);
    int flag = 0;
    // Read the first block of the file into the buffer
    off_t first_block_offset = BLOCK_OFFSET(inode.i_block[0]);
    lseek(img_fd, first_block_offset, SEEK_SET);
    read(img_fd, buffer, block_size);     
    if (buffer[0] == (char)0xff &&
        buffer[1] == (char)0xd8 &&
        buffer[2] == (char)0xff &&
        (buffer[3] == (char)0xe0 ||
        buffer[3] == (char)0xe1 ||
        buffer[3] == (char)0xe8)) 
    {
        // jpg signature found
        flag = 1;
    }
    free(buffer);
    return flag;
}

void search_for_filename(int inode_number, char* original_filename)
{
    // Put filename in original_filename and return
    int a = inode_number;
    char* b = original_filename;
    a++;
    b++;
    return;
}

void write_file_contents_to_output(int fd, struct ext2_inode** inode_ref, FILE* file_copy, char* file_contents_buffer)
{
    // Calculate the number of blocks required to store the file
    struct ext2_inode* inode = *inode_ref;
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
        ssize_t bytes_read = read(fd, file_contents_buffer, block_size);

        // Write the block to the jpg file
        ssize_t bytes_to_write = bytes_read;
        if (k == blocks_required - 1) { // Last block
            bytes_to_write = inode->i_size - block_size * (blocks_required - 1);
        }
        fwrite(file_contents_buffer, 1, bytes_to_write, file_copy);
    }
    free(single_indirect_block);
    free(double_indirect_block);
}
