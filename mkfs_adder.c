#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t checksum;
} superblock_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[12];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;
    uint64_t inode_crc;
} inode_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)

uint32_t CRC32_TAB[256];
void crc32_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        CRC32_TAB[i] = c;
    }
}

uint32_t crc32(const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c = CRC32_TAB[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

void inode_crc_finalize(inode_t *ino) {
    uint8_t tmp[INODE_SIZE];
    memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

void dirent_checksum_finalize(dirent64_t *de) {
    const uint8_t *p = (const uint8_t *)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];
    de->checksum = x;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s --input <input_img> --output <output_img> --file <file_to_add>\n", argv[0]);
        return 1;
    }

    const char *input_img = argv[1];
    const char *output_img = argv[2];
    const char *file_to_add = argv[3];

    FILE *img_file = fopen(input_img, "rb+");
    if (!img_file) {
        perror("Failed to open input image");
        return 1;
    }

    superblock_t superblock;
    fread(&superblock, sizeof(superblock_t), 1, img_file);

    fseek(img_file, BS, SEEK_SET);
    dirent64_t existing_entry;
    const char *filename_to_add = strrchr(file_to_add, '/');
    filename_to_add = filename_to_add ? filename_to_add + 1 : file_to_add;
    
    size_t entries_checked = 0;
    size_t max_entries = BS / sizeof(dirent64_t);
    
    while (entries_checked < max_entries) {
        size_t bytes_read = fread(&existing_entry, sizeof(dirent64_t), 1, img_file);
        if (bytes_read != 1) break;
        
        if (existing_entry.inode_no != 0) {
            if (strncmp(existing_entry.name, filename_to_add, 58) == 0) {
                printf("Error: Cannot add same file twice. File '%s' already exists in the image.\n", filename_to_add);
                fclose(img_file);
                return 1;
            }
        }
        entries_checked++;
    }

    FILE *file = fopen(file_to_add, "rb");
    if (!file) {
        perror("Failed to open file to add");
        fclose(img_file);
        return 1;
    }

    uint8_t buffer[BS];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BS, file)) > 0) {
        fwrite(buffer, 1, bytes_read, img_file);
    }

    fclose(file);

    dirent64_t dir_entry = {0};
    dir_entry.inode_no = ROOT_INO;
    dir_entry.type = 1;
    strncpy(dir_entry.name, filename_to_add, 57);
    dir_entry.name[57] = '\0';
    dirent_checksum_finalize(&dir_entry);

    fseek(img_file, BS, SEEK_SET);
    dirent64_t temp_entry;
    size_t entry_offset = 0;
    bool found_slot = false;
    
    entries_checked = 0;
    while (entries_checked < max_entries) {
        long current_pos = ftell(img_file);
        size_t bytes_read = fread(&temp_entry, sizeof(dirent64_t), 1, img_file);
        
        if (bytes_read != 1 || temp_entry.inode_no == 0) {
            fseek(img_file, current_pos, SEEK_SET);
            found_slot = true;
            break;
        }
        entries_checked++;
    }
    
    if (!found_slot) {
        printf("Error: Directory is full, cannot add more files.\n");
        fclose(img_file);
        return 1;
    }

    fwrite(&dir_entry, sizeof(dirent64_t), 1, img_file);

    fclose(img_file);
    printf("File '%s' added to the image '%s'.\n", filename_to_add, input_img);
    return 0;
}
