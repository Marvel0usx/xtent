#include "util.h"

#ifndef HELPERS_INCLUDED
#define HELPERS_INCLUDED

/** Get address of block at the given block number. */
void *jump_to(void *image, uint32_t idx, size_t unit)
{
    return image + idx * unit;
}

/** Set all bit to 0 in the given bitmap. */
void reset_bitmap(unsigned char *bitmap)
{
    memset(bitmap, 0, A1FS_BLOCK_SIZE);
}

/** Get the superblock. */
a1fs_superblock *get_superblock(void *image)
{
    return (a1fs_superblock *)image;
}

/** Get the first inode bitmap. */
unsigned char *get_first_inode_bitmap(void *image)
{
    a1fs_superblock *s = (a1fs_superblock *)image;
    return (unsigned char *)jump_to(image, s->s_inode_bitmap, A1FS_BLOCK_SIZE);
}

/** Get the first data bitmap. */
unsigned char *get_first_data_bitmap(void *image)
{
    a1fs_superblock *s = (a1fs_superblock *)image;
    return (unsigned char *)jump_to(image, s->s_data_bitmap, A1FS_BLOCK_SIZE);
}

// locate which bitmap stores this bit
a1fs_blk_t get_block_offset(uint32_t bit)
{
    return (a1fs_blk_t)bit / A1FS_BLOCK_SIZE;
}

// locate which byte stores this bit
uint32_t get_byte_offset(uint32_t bit)
{
    return (bit - ((bit / A1FS_BLOCK_SIZE) * A1FS_BLOCK_SIZE)) / sizeof(char);
}

// locate the bit in byte
uint32_t get_bit_offset(uint32_t bit)
{
    return bit % sizeof(char);
}

/** Check if the bit is used. */
bool is_used_bit(void *image, uint32_t bit, uint32_t lookup)
{
    a1fs_superblock *s = get_superblock(image);
    // choose which bitmap
    unsigned char *bitmap;
    if (lookup == LOOKUP_DB)
    {
        if (s->s_num_free_blocks <= 0)
            return true;
        if (bit >= s->s_num_blocks)
        {
            fprintf(stderr, "Invalid lookup of data bitmap at %d\n", bit);
            perror("CORE ERROR.\n");
        }
        else
        {
            bitmap = (unsigned char *)jump_to(image, s->s_data_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        }
    }
    else if (lookup == LOOKUP_IB)
    {
        if (s->s_num_free_inodes <= 0)
            return true;
        if (bit >= s->s_num_inodes)
        {
            fprintf(stderr, "Invalid lookup of inode bitmap at %d\n", bit);
            perror("CORE ERROR.\n");
        }
        else
        {
            bitmap = (unsigned char *)jump_to(image, s->s_inode_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        }
    }
    else
    {
        perror("Invalid lookup flag.\n");
    }
    return (bitmap[get_byte_offset(bit)] & (1 << get_bit_offset(bit))) == 0;
}

void _mask(unsigned char *bitmap, uint32_t bit)
{
    // set the bit to 1.
    bitmap[get_byte_offset(bit)] |= (1 << get_bit_offset(bit));
}

/** Set the bit to 1 in the bitmap indicated by lookup. */
void mask(void *image, uint32_t bit, uint32_t lookup)
{
    a1fs_superblock *s = get_superblock(image);
    if (!is_used_bit(image, bit, lookup))
    {
        fprintf(stderr, "Cannot mask used bit at %d.\n", bit);
        return;
    }
    // find the correct bitmap that contains this bit and mark to 1.
    unsigned char *bitmap;
    if (lookup == LOOKUP_DB)
    {
        bitmap = (unsigned char *)jump_to(image, s->s_data_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        _mask(bitmap, bit);
        s->s_num_free_blocks--;
    }
    else if (lookup == LOOKUP_IB)
    {
        bitmap = (unsigned char *)jump_to(image, s->s_inode_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        _mask(bitmap, bit);
        s->s_num_free_inodes--;
    }
}

/** Mask from start to end (exclusive) in bitmap. */
void mask_range(void *image, uint32_t offset_start, uint32_t offset_end, uint32_t lookup)
{
    for (uint32_t offset = offset_start; offset < offset_end; offset++)
    {
        mask(image, offset, lookup);
    }
}

/** Find the first unused bit. Return -1 if no free block found. */
uint32_t find_first_free_blk_num(void *image, uint32_t lookup)
{
    a1fs_superblock *s = get_superblock(image);
    if (lookup == LOOKUP_DB)
    {
        if (s->s_num_free_blocks <= 0)
        {
            return -1;
        }
        for (uint32_t bit = 0; bit < s->s_num_blocks; bit++)
        {
            if (!is_used_bit(image, bit, lookup))
            {
                return bit;
            }
        }
    }
    else if (lookup == LOOKUP_IB)
    {
        if (s->s_num_free_inodes <= 0)
        {
            return -1;
        }
        for (uint32_t bit = 0; bit < s->s_num_inodes; bit++)
        {
            if (!is_used_bit(image, bit, lookup))
            {
                return bit;
            }
        }
    }
    else
    {
        perror("Invalid lookup.");
    }
    return -1;
}

/** Initialize empty directory block. */
void init_directory_blk(void *image, a1fs_blk_t blk_num)
{
    a1fs_dentry *dir = (a1fs_dentry *)jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t idx = 0; idx < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); idx++)
    {
        (dir + idx)->ino = -1;
    }
}

/** Find first unused directory entry in the block given its block number. 
 * return the offset of the directory, -1 for excess the max: 512.
*/
uint32_t find_first_empty_direntry(void *image, a1fs_blk_t blk_num)
{
    a1fs_superblock *s = get_superblock(image);
    a1fs_dentry *dir = (a1fs_dentry *)jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); offset++)
    {
        if ((dir + offset)->ino >= s->s_num_inodes)
        {
            return offset;
        }
    }
    return -1;
}

uint32_t get_itable_block_offset(a1fs_ino_t inum)
{
    return inum / (A1FS_BLOCK_SIZE / sizeof(a1fs_inode));
}

uint32_t get_itable_offset(a1fs_ino_t inum)
{
    return inum % (A1FS_BLOCK_SIZE / sizeof(a1fs_inode));
}

/** Get inode by inum. */
a1fs_inode *get_inode_by_inumber(void *image, a1fs_ino_t inum)
{
    a1fs_superblock *s = get_superblock(image);
    // check if the inumber is valid.
    if (inum >= s->s_num_inodes)
    {
        fprintf(stderr, "Invalid inumber %d", inum);
        return NULL;
    }
    uint32_t itable_blk_offset = get_itable_block_offset(inum) + s->s_inode_table;
    uint32_t itable_offset = get_itable_offset(inum);

    a1fs_inode *itable = (a1fs_inode *)jump_to(image, itable_blk_offset, A1FS_BLOCK_SIZE);
    return (itable + itable_offset);
}

#endif