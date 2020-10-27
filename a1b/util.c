#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "util.h"
#include "fs_ctx.h"

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
    return (bit - ((bit / A1FS_BLOCK_SIZE) * A1FS_BLOCK_SIZE)) / 8;
}

// locate the bit in byte
uint32_t get_bit_offset(uint32_t bit)
{
    return bit % 8;
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
    return (bitmap[get_byte_offset(bit)] & (1 << get_bit_offset(bit))) != 0;
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
    if (is_used_bit(image, bit, lookup))
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
int find_first_free_blk_num(void *image, uint32_t lookup)
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
        (dir + idx)->ino = (a1fs_ino_t) NULL;
    }
}

/** Find first unused directory entry in the block given its block number. 
 * return the offset of the directory, -1 for excess the max: 512.
*/
int find_first_empty_direntry_offset(void *image, a1fs_blk_t blk_num)
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

/** Format the block to empty extents. */
void init_extent_blk(void *image, a1fs_blk_t blk_num) {
    (a1fs_extent *) extent_start = (a1fs_extent *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); offset++) {
        // set the start of the extent to NULL, indicating unused
        (extent_start + offset)->start = NULL;
    }
}

/* Find the first empty extent. Return the offset to the extent that is not used. If 
 * no empty extent found, return -1.
*/
int find_first_empty_extent_offset(void *image, a1fs_blk_t blk_num) {
    (a1fs_extent *) extent_start = (a1fs_extent *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); offset++) {
        // unused if the start is NULL
        if ((extent_start + offset)->start == NULL) {
            return offset;
        }
    }
    return -1;
}

/** Find the inumber of the file given its name, starting from dir. 
 * Return -1 if not found.
 */
int find_file_ino_in_dir(void *image, a1fs_inode *dir_ino, char *name) {
    a1fs_extent *this_extent = (a1fs_extent *) jump_to(image, dir_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    while (this_extent->start != NULL) {
        a1fs_dentry *this_dentry;
        for (a1fs_blk_t blk_offset = 0; blk_offset < this_extent->count; blk_offset++) {
            for (uint32_t dentry_offset = 0; dentry_offset < 512; dentry_offset++ ) {
                uint32_t blk_num = this_extent->start + blk_offset;
                this_dentry = (a1fs_dentry *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
                this_dentry += dentry_offset;
                if (strcmp(name, this_dentry->name) == 0) {
                    return this_dentry->ino;
                }
            }
        }
        this_extent++;
    }
    return -1;
}

/** Recursion helper for path traversal. */
static int path_lookup_helper(char *path, a1fs_ino_t inumber, fs_ctx *fs) {
    if (path == NULL) {
        return inumber;     // Reaches to the end.
    }
    a1fs_inode *this_inode = get_inode_by_inumber(fs->image, inumber);
    if (S_ISREG(this_inode->mode) != 0) {
        // Bad path: a component is not a directory
        return -ENOTDIR;
    } else {
        char *filename = strsep(&path, "/");
        int err = find_file_ino_in_dir(fs->image, this_inode, filename);
        // file is not in directory
        if (err == -1) {
            return -ENOENT;
        } else {
            return path_lookup_helper(path, err, fs);
        }
    }
}


/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
int path_lookup(const char *path, fs_ctx *fs) {
    // check if the path is an absolute path
    if(path[0] != '/') {
        fprintf(stderr, "Not an absolute path\n");
        // the starting point is not in the root-dir anyhow
        return -ENOENT;
    }
    char *path_copy, *path_original;
    path_copy = path_original = strdup(path);
    strsep(&path_copy, "/");
    int err;
    if (strcmp(path_copy, "") == 0) {
        // path is the root dir
        err = 0;
    } else {
        err = path_lookup_helper(path_copy, 0, fs);
    }
    free(path_original);
    return err;
}

#endif
