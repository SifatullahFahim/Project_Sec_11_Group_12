#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
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

_Static_assert(sizeof(superblock_t) == 112, "superblock must fit in one block");

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

_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size mismatch");

#pragma pack(push, 1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)

_Static_assert(sizeof(dirent64_t) == 64, "dirent size mismatch");

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

static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s --image <image_name> --size-kib <size_kib> --inodes <inode_count>\n", argv[0]);
        return 1;
    }

    uint64_t size_kib = atoi(argv[2]);
    uint64_t inode_count = atoi(argv[3]);

    superblock_t superblock = {0};
    superblock.magic = 0x4D565346;
    superblock.version = 1;
    superblock.block_size = BS;
    superblock.total_blocks = size_kib * 1024 / BS;
    superblock.inode_count = inode_count;
    superblock.root_inode = ROOT_INO;
    superblock.mtime_epoch = (uint64_t)time(NULL);
    
    superblock_crc_finalize(&superblock);

    FILE *image = fopen(argv[1], "wb");
    if (!image) {
        perror("Failed to create image");
        return 1;
    }

    fwrite(&superblock, sizeof(superblock_t), 1, image);

    uint8_t block[BS] = {0};
    for (uint64_t i = 1; i < superblock.total_blocks; i++) {
        fwrite(block, sizeof(block), 1, image);
    }

    fclose(image);
    printf("File system image '%s' created successfully.\n", argv[1]);
    return 0;
}
