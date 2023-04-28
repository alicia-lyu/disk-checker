#include "ext2_fs.h"
#include "read_ext2.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

struct jpg_file {
    __u32 inode_number;
    struct ext2_inode* inode;
    char filename[EXT2_NAME_LEN];
    struct jpg_file* next;
};

void write_file_contents_to_output(int fd, struct ext2_inode* inode, FILE* file_copy, char* file_contents_buffer);
int create_dir(char* dirname);
void add_inode_to_lk(struct jpg_file** head_ref, int num_groups, struct ext2_group_desc* groups, int fd, struct ext2_super_block* super);
void add_filename_to_lk(struct jpg_file** head_ref, int num_groups, struct ext2_group_desc* groups, int fd, struct ext2_super_block* super);
int get_offset_with_name_len(struct ext2_dir_entry_2* dir_entry);
int is_jpg(struct ext2_inode* inode, int img_fd);
int write_file_details(const char *path, struct ext2_inode *inode);
int copy_file_with_new_name(char* path, int img_fd, struct ext2_inode* inode);

int main(int argc, char **argv) 
{
    if (argc != 3) 
    {
        printf("expected usage: ./runscan inputfile outputfile\n");
        exit(0);
    }

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

    struct jpg_file* head = malloc(sizeof(struct jpg_file));
    head->next = NULL;
    head->inode = NULL;
    head->inode_number = 0;

    // get inodes of all jpgs
    add_inode_to_lk(&head, num_groups, groups, fd, &super);
    
    // add filenames to jpg_files
    add_filename_to_lk(&head, num_groups, groups, fd, &super);
    
    struct jpg_file* current;
    struct jpg_file* next_node;

    // copy jpgs to output directory with different filenames
    current = head->next;
    while (current != NULL) {
        struct jpg_file* jpg_file = current;
        // copy the data to the path with inode
        char path_inode[100];
        sprintf(path_inode, "%s/file-%d.jpg", argv[2], jpg_file->inode_number);
        int copy_ret = copy_file_with_new_name(path_inode, fd, jpg_file->inode);
        if (copy_ret) exit(1);

        // copy the data to the path with filename
        char path_filename[EXT2_NAME_LEN + 100];
        sprintf(path_filename, "%s/%s", argv[2], jpg_file->filename);
        copy_ret = copy_file_with_new_name(path_filename, fd, jpg_file->inode);
        if (copy_ret) exit(1);

        // write details file
        char path_details[100];
        sprintf(path_details, "%s/file-%d-details.txt", argv[2], jpg_file->inode_number);
        char* details = (char*) malloc(100);
        sprintf(details, "%d\n", jpg_file->inode_number);
        int write_details_ret = write_file_details(path_details, jpg_file->inode);
        if (write_details_ret) exit(1);
        free(details);
        current = current->next;
    }

    // free linked list
    current = head;
    while (current != NULL) {
        next_node = current->next;
        free(current->inode);
        free(current);
        current = next_node;
    }
    free(groups);
    close(fd);
    return 0;
}

int create_dir(char* dirname)
{
    // Create directory argv[2] if not exists using mkdir, detect existence using opendir
    DIR* dir = opendir(dirname);
    if (dir) {
        closedir(dir);
        if (debug)
            printf("Output dir already exists\n");
        return 0;
    } else if (errno == ENOENT) {
        mkdir(dirname, 0777);
        return 0;
    } else {
        printf("Error creating directory\n");
        return 1;
    }
}

void add_inode_to_lk(struct jpg_file** head_ref, int num_groups, struct ext2_group_desc* groups, int fd, struct ext2_super_block* super) 
{
    struct jpg_file* current = *head_ref;
    struct jpg_file* next_node;
    
    for (int i = 0; i < num_groups; i++) 
    {
        off_t inode_table_offset = locate_inode_table(i, groups);
        // Iterate through all the inodes in the block group
        for (__u32 j = 0; j < super->s_inodes_per_group; j++) 
        {
            struct ext2_inode* inode = malloc(sizeof(struct ext2_inode));
            read_inode(fd, inode_table_offset, j + 1, inode, super->s_inode_size); // ext 2 inode numbers start at 1
            if (S_ISREG(inode->i_mode)) {
                if (debug){
                    printf("Found inode %d\n", j + 1);
                    printf("File size: %d\n", inode->i_size);
                }
                // Inspect if file is jpg
                int is_jpg_flag = is_jpg(inode, fd);
                if (!is_jpg_flag) continue;
                if (debug) printf("Inode %d is a jpg\n", j + 1);
                // add to the linked list
                next_node = (struct jpg_file*) malloc(sizeof(struct jpg_file));
                next_node->inode_number = j + 1;
                next_node->inode = malloc(sizeof(struct ext2_inode));
                memcpy(next_node->inode, inode, sizeof(struct ext2_inode));
                printf("Copied inode %u, size %u, links count %u, owner id %u\n", 
                    next_node->inode_number, 
                    next_node->inode->i_size, 
                    next_node->inode->i_links_count, 
                    next_node->inode->i_uid);
                current->next = next_node;
                current = next_node;
            }
            free(inode);
        }
    }
}

void add_filename_to_lk(struct jpg_file** head_ref, int num_groups, struct ext2_group_desc* groups, int fd, struct ext2_super_block* super)
{   
    struct jpg_file* current;
    for (int i = 0; i < num_groups; i++) 
    {
        off_t inode_table_offset = locate_inode_table(i, groups);
        // Iterate through all the inodes in the block group
        for (__u32 j = 0; j < super->s_inodes_per_group; j++) 
        {
            struct ext2_inode inode;
            read_inode(fd, inode_table_offset, j + 1, &inode, super->s_inode_size); // ext 2 inode numbers start at 1
            if (S_ISDIR(inode.i_mode)) {
                // store all dir entries in dir_entries
                struct ext2_dir_entry_2* dir_entries = (struct ext2_dir_entry_2*) malloc(block_size);
                lseek(fd, BLOCK_OFFSET(inode.i_block[0]), SEEK_SET);
                read(fd, dir_entries, block_size);
                // iterate through all dir entries
                void* ptr = (void*) dir_entries;
                while (ptr < (void*) dir_entries + block_size) {
                    struct ext2_dir_entry_2* dir_entry = ((struct ext2_dir_entry_2*) ptr);
                    if (dir_entry->inode == 0) {
                        ptr += get_offset_with_name_len(dir_entry);
                        continue;
                    }
                    // get filename of dir_entry
                    char filename[EXT2_NAME_LEN];
                    int name_len = dir_entry->name_len & 0xFF;
                    if (debug) {
                        printf("rec_len: %d\n", dir_entry->rec_len);
                        printf("name_len: %d\n", name_len);
                        printf("calculated len: %d\n", get_offset_with_name_len(dir_entry));
                    }
                    strncpy(filename, dir_entry->name, name_len);
                    filename[name_len] = '\0';
                    if (debug) printf("Found directory entry: %d, %s\n", dir_entry->inode, dir_entry->name);
                    if (strcmp(filename, ".") == 0 || strcmp(filename, "..") == 0) {
                        ptr += get_offset_with_name_len(dir_entry);
                        continue;
                    }
                    // store filename in linked list if it is a jpg
                    current = *head_ref;
                    while (current != NULL) {
                        if (current->inode_number == dir_entry->inode) {
                            strcpy(current->filename, filename);
                            if (debug) printf("Found filename of jpg: %s with inode number %d\n", filename, dir_entry->inode);
                            break;
                        }
                        current = current->next;
                    }
                    
                    ptr += get_offset_with_name_len(dir_entry);
                }
                free(dir_entries);
            }
        }
    }
}

int get_offset_with_name_len(struct ext2_dir_entry_2* dir_entry) {
    int name_len = dir_entry->name_len & 0xFF;
    return ((name_len + 8 + 4-1) / 4) * 4;
}

int write_file_details(const char *path, struct ext2_inode *inode) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        printf("Error creating file\n");
        return 1;
    }

    fprintf(file, "%u\n", inode->i_links_count);
    fprintf(file, "%u\n", inode->i_size);
    fprintf(file, "%u", inode->i_uid);

    fclose(file);
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
    write_file_contents_to_output(img_fd, inode, file_copy, file_contents_buffer);
    // write file details
    fclose(file_copy);
    free(file_contents_buffer);
    return 0;
}

int is_jpg(struct ext2_inode* inode, int img_fd) 
{
    char* buffer = (char*) malloc(block_size);
    int flag = 0;
    // Read the first block of the file into the buffer
    off_t first_block_offset = BLOCK_OFFSET(inode->i_block[0]);
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

void write_file_contents_to_output(int fd, struct ext2_inode* inode, FILE* file_copy, char* file_contents_buffer)
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
