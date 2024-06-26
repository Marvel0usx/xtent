#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

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

static void _mask(unsigned char *bitmap, uint32_t bit, bool on)
{
    if (on)
        bitmap[get_byte_offset(bit)] |= (1 << get_bit_offset(bit));
    else
        bitmap[get_byte_offset(bit)] &= ~(1 << get_bit_offset(bit));
}

/** Set the bit to 1 in the bitmap indicated by lookup. */
void mask(void *image, uint32_t bit, uint32_t lookup, bool on)
{
    a1fs_superblock *s = get_superblock(image);
    if (on && is_used_bit(image, bit, lookup))
    {
        fprintf(stderr, "Cannot mask used bit at %d.\n", bit);
        return;
    }
    // find the correct bitmap that contains this bit and mark to 1.
    unsigned char *bitmap;
    if (lookup == LOOKUP_DB)
    {
        bitmap = (unsigned char *)jump_to(image, s->s_data_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        _mask(bitmap, bit, on);
        if (on)
            s->s_num_free_blocks--;
        else
            s->s_num_free_blocks++;
    }
    else if (lookup == LOOKUP_IB)
    {
        bitmap = (unsigned char *)jump_to(image, s->s_inode_bitmap + get_block_offset(bit), A1FS_BLOCK_SIZE);
        _mask(bitmap, bit, on);
        if (on)
            s->s_num_free_inodes--;
        else
            s->s_num_free_inodes++;
    }
}

/** Mask from start to end (exclusive) in bitmap. */
void mask_range(void *image, uint32_t offset_start, uint32_t offset_end, uint32_t lookup, bool on)
{
    for (uint32_t offset = offset_start; offset < offset_end; offset++)
    {
        mask(image, offset, lookup, on);
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
        (dir + idx)->ino = (a1fs_ino_t) -1;
    }
}

/** Find first unused directory entry in the block given its block number. 
 * return the offset of the directory, -1 for excess the max: 512.
*/
int find_first_empty_direntry_offset(void *image, a1fs_blk_t blk_num)
{
    a1fs_dentry *dir = (a1fs_dentry *)jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_dentry); offset++)
    {
        if ((dir + offset)->ino == (a1fs_ino_t) -1)
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
    a1fs_extent *extent_start = (a1fs_extent *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); offset++) {
        // set the start of the extent to -1, indicating unused
        (extent_start + offset)->start = (a1fs_blk_t) -1;
    }
}

/* Find the first empty extent. Return the offset to the extent that is not used. If 
 * no empty extent found, return -1.
*/
int find_first_empty_extent_offset(void *image, a1fs_blk_t blk_num) {
    a1fs_extent *extent_start = (a1fs_extent *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    for (uint32_t offset = 0; offset < A1FS_BLOCK_SIZE / sizeof(a1fs_extent); offset++) {
        // unused if the start is -1
        if ((extent_start + offset)->start == (a1fs_blk_t) -1) {
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
    for (a1fs_blk_t extent_offset = 0; extent_offset < 512; extent_offset++) {
        if ((this_extent + extent_offset)->start == (a1fs_blk_t) -1) continue;
        a1fs_dentry *this_dentry;
        for (a1fs_blk_t blk_offset = 0; blk_offset < (this_extent + extent_offset)->count; blk_offset++) {
            uint32_t blk_num = (this_extent + extent_offset)->start + blk_offset;
            this_dentry = (a1fs_dentry *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
            for (uint32_t dentry_offset = 0; dentry_offset < 16; dentry_offset++ ) {
                if ((this_dentry + dentry_offset)->ino == (a1fs_ino_t) -1) continue;
                int err = strcmp(name, (this_dentry + dentry_offset)->name);
                if (err == 0) {
                    return (this_dentry + dentry_offset)->ino;
                }
            }
        }
    }
    return -1;
}

/** Recursion helper for path traversal. */
static int path_lookup_helper(char *path, a1fs_ino_t inumber, fs_ctx *fs) {
    a1fs_inode *this_inode = get_inode_by_inumber(fs->image, inumber);
    if (this_inode == NULL) perror("Invalid inode");
    if (S_ISREG(this_inode->mode) != 0) {
        // Bad path: a component is not a directory
        return -ENOTDIR;
    } else {
        char *filename = strsep(&path, "/");
        int err = find_file_ino_in_dir(fs->image, this_inode, filename);
        // file is not in directory
        if (err == -1) {
            return -ENOENT;
        } else if (path == NULL || strcmp(path, "") == 0) {
            // here, we have reached to the end
            return err;
        } else {
            return path_lookup_helper(path, err, fs);
        }
    }
}

/** Find first usable directory entry. Return NULL if no empty dentry => either abort or extend
 * existing extent.
*/
a1fs_dentry *find_first_free_dentry(void *image, a1fs_ino_t inum) {
    a1fs_inode *ino = get_inode_by_inumber(image, inum);
    a1fs_extent *start_extent = (a1fs_extent *) jump_to(image, ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_extent *this_extent = start_extent; this_extent - start_extent < 512; this_extent++) {
        if (this_extent->start == (a1fs_blk_t) -1) continue;
        for (a1fs_blk_t blk_offset = 0; blk_offset < this_extent->count; blk_offset++) {
            int dentry_offset = find_first_empty_direntry_offset(image, this_extent->start + blk_offset);
            // no available dentry in this block
            if (dentry_offset == -1) continue;
            // else
            a1fs_dentry *res = (a1fs_dentry *) jump_to(image, this_extent->start + blk_offset, A1FS_BLOCK_SIZE);
            return res + dentry_offset;
        }
    }
    return NULL;
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

/** Initialize inode. */
void init_inode(void *image, a1fs_ino_t inum, mode_t mode, 
uint32_t links, uint64_t size, uint32_t extents, a1fs_blk_t ptr_extent) {
    a1fs_inode *this_node = get_inode_by_inumber(image, inum);
    this_node->mode = mode;
    this_node->links = links;
    this_node->size = size;
    clock_gettime(CLOCK_REALTIME, &(this_node->mtime));
    this_node->i_extents = extents;
    this_node->i_ptr_extent = ptr_extent;
}

/** Create new dir in dentry. */
void create_new_dir_in_dentry(void *image, a1fs_dentry *parent_dir, const char *name, mode_t mode) {
	a1fs_ino_t inum = find_first_free_blk_num(image, LOOKUP_IB);
    // init new extent block for new dir
    a1fs_blk_t ext_blk_num = find_first_free_blk_num(image, LOOKUP_DB);
    init_extent_blk(image, ext_blk_num);
    mask(image, ext_blk_num, LOOKUP_DB, true);
    // init new dentry block for new dir
    a1fs_blk_t dentry_blk_num = find_first_free_blk_num(image, LOOKUP_DB);
    init_directory_blk(image, dentry_blk_num);
    mask(image, dentry_blk_num, LOOKUP_DB, true);
    // set the first extent to the dentry block
    a1fs_extent *ext_new_dir = (a1fs_extent *) jump_to(image, ext_blk_num, A1FS_BLOCK_SIZE);
    ext_new_dir->start = dentry_blk_num;
    ext_new_dir->count = 1;
    // init new dir's inode
    init_inode(image, inum, mode, 1, 0, 1, ext_blk_num);
    mask(image, inum, LOOKUP_IB, true);
    // record in parent dentry
    parent_dir->ino = inum;
    strncpy(parent_dir->name, name, A1FS_NAME_MAX);
    int len = strlen(name);
    if (len < A1FS_NAME_MAX)
        parent_dir->name[strlen(name)] = '\0';
    else
        parent_dir->name[A1FS_NAME_MAX - 1] = '\0';
}

/** Traverse all extent of dir_ino to find name; return the inum of the file. */
a1fs_dentry *find_dentry_in_dir(void *image, a1fs_inode *dir_ino, const char *name) {
    a1fs_extent *this_extent = (a1fs_extent *) jump_to(image, dir_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_blk_t extent_offset = 0; extent_offset < 512; extent_offset++) {
        if ((this_extent + extent_offset)->start == (a1fs_blk_t) -1) continue;
        a1fs_dentry *this_dentry;
        for (a1fs_blk_t blk_offset = 0; blk_offset < (this_extent + extent_offset)->count; blk_offset++) {
            uint32_t blk_num = (this_extent + extent_offset)->start + blk_offset;
            this_dentry = (a1fs_dentry *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
            for (uint32_t dentry_offset = 0; dentry_offset < 16; dentry_offset++ ) {
                if ((this_dentry + dentry_offset)->ino == (a1fs_ino_t) -1) continue;
                int err = strcmp(name, (this_dentry + dentry_offset)->name);
                if (err == 0) {
                    return this_dentry + dentry_offset;
                }
            }
        }
    }
    return NULL;
}

/** Return true if all associated dentry is empty, else false. */
bool is_empty_dir(void *image, a1fs_inode *dir_ino) {
    a1fs_extent *this_extent = (a1fs_extent *) jump_to(image, dir_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_blk_t extent_offset = 0; extent_offset < 512; extent_offset++) {
        if ((this_extent + extent_offset)->start == (a1fs_blk_t) -1) continue;
        a1fs_dentry *this_dentry;
        for (a1fs_blk_t blk_offset = 0; blk_offset < (this_extent + extent_offset)->count; blk_offset++) {
            uint32_t blk_num = (this_extent + extent_offset)->start + blk_offset;
            this_dentry = (a1fs_dentry *) jump_to(image, blk_num, A1FS_BLOCK_SIZE);
            for (uint32_t dentry_offset = 0; dentry_offset < 16; dentry_offset++ ) {
                if ((this_dentry + dentry_offset)->ino != (a1fs_ino_t) -1) {
                    return false;
                }
            }
        }
    }
    return true;
}

/** Find all blk num of dentry blk associated with ino, mask the blk in bitmap as 0. */
void free_dentry_blks(void *image, a1fs_inode *dir_ino) {
    a1fs_extent *this_extent = (a1fs_extent *) jump_to(image, dir_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_blk_t extent_offset = 0; extent_offset < 512; extent_offset++) {
        if ((this_extent + extent_offset)->start == (a1fs_blk_t) -1) continue;
        for (a1fs_blk_t blk_offset = 0; blk_offset < (this_extent + extent_offset)->count; blk_offset++) {
            uint32_t blk_num = (this_extent + extent_offset)->start + blk_offset;
            mask(image, blk_num, LOOKUP_DB, false); 
        }
    }
}

/** Create an empty file inside the directory. */
void create_new_file_in_dentry(void *image, a1fs_dentry *dir, const char *name, mode_t mode) {
    a1fs_ino_t new_file_inum = find_first_free_blk_num(image, LOOKUP_IB);
    a1fs_blk_t new_file_ext_bnum = find_first_free_blk_num(image, LOOKUP_DB);
    init_extent_blk(image, new_file_ext_bnum);
    mask(image, new_file_ext_bnum, LOOKUP_DB, true);
    init_inode(image, new_file_inum, mode, 1, 0, 0, new_file_ext_bnum);
    mask(image, new_file_inum, LOOKUP_IB, true);
    dir->ino = new_file_inum;
    strncpy(dir->name, name, A1FS_NAME_MAX);
    dir->name[strlen(name)] = '\0';
}

/** Find the last used extent. */
a1fs_extent *find_last_used_ext(void *image, a1fs_inode *ino) {
    a1fs_extent *last_used = NULL;
    a1fs_extent *this = (a1fs_extent *)jump_to(image, ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_blk_t offset = 0; offset < 512; offset++) {
        if ((this + offset)->start != (a1fs_blk_t) -1) {
            last_used = (this + offset);
        }
    }
    return last_used;
}

/** Shrink the extent by n block. Mask off blocks and unset extent if 
 * the extent is empty. Return the number of extent reduced. */
int shrink_ext_by_num_blk(void *image, a1fs_extent *ext, a1fs_blk_t *num) {
    a1fs_blk_t ext_size = ext->count;
    if (*num >= ext_size) {
        // delete whole extent and free blocks
        for (a1fs_blk_t offset = 0; offset < ext_size; offset++) {
            void *blk = jump_to(image, ext->start + offset, A1FS_BLOCK_SIZE);
            // erase block
            memset(blk, 0, A1FS_BLOCK_SIZE);
            // mask block to unused
            mask(image, ext->start + offset, LOOKUP_DB, false);
        }
        ext->start = (a1fs_blk_t) -1;
        *num -= ext_size;
        return 1;
    } else {
        // shrink by num
        for (a1fs_blk_t offset = 1; offset < *num + 1; offset++) {
            void *blk = jump_to(image, ext->start + ext->count - offset, A1FS_BLOCK_SIZE);
            memset(blk, 0, A1FS_BLOCK_SIZE);
            mask(image, ext->start + ext->count - offset, LOOKUP_DB, false);
        }
        *num = 0;
        return 0;
    }
}

/** Shrink the block to the given size in byte. */
void shrink_blk_to_size(void *image, a1fs_blk_t blk_num, size_t size) {
    unsigned char *blk = (unsigned char *)jump_to(image, blk_num, A1FS_BLOCK_SIZE);
    blk += size;
    memset(blk, 0, A1FS_BLOCK_SIZE - size);
};

/** Shrink the file by num of block. */
void shrink_by_num_blk(void *image, a1fs_inode *ino, a1fs_blk_t num_blk) {
    a1fs_extent *last_ext;
    while (num_blk > 0) {
        last_ext = find_last_used_ext(image, ino);
        ino->i_extents -= shrink_ext_by_num_blk(image, last_ext, &num_blk);
    }
};

/** Shrink amount of bytes specified in size. */
int shrink_by_amount(void *image, a1fs_inode *ino, size_t size) {
    // remove the non-block ending of the file
    size_t tailing = ino->size % A1FS_BLOCK_SIZE;
    a1fs_extent *last_extent = find_last_used_ext(image, ino);
    if (tailing != 0) {
        a1fs_blk_t last_blk = last_extent->start + last_extent->count - 1;
        if (tailing > size) {
            // shrink in tailing block
            shrink_blk_to_size(image, last_blk, tailing - size);
            size = 0;
        } else {
            // remove tailing block
            shrink_blk_to_size(image, last_blk, 0);
            last_extent->count--;
            // remove extent if empty
            if (last_extent->count == 0) {
                last_extent->start = (a1fs_blk_t) -1;
                ino->i_extents--;
            }
            // update the amount
            size -= tailing;
        }
    }
    a1fs_blk_t shrink_blk = size / A1FS_BLOCK_SIZE;
    size_t shrink_to_bytes = A1FS_BLOCK_SIZE - size % A1FS_BLOCK_SIZE;
    // shrink by blocks
    if (shrink_blk != 0)
        shrink_by_num_blk(image, ino, shrink_blk);
    // now shrink within one block
    if (shrink_to_bytes != 0) {
        last_extent = find_last_used_ext(image, ino);
        shrink_blk_to_size(image, last_extent->start + last_extent->count - 1, shrink_to_bytes);
    }
    return 0;
}

/** Find the starting block num for consecutive n. Return -1 is unfound. */
static a1fs_blk_t window_slide(fs_ctx *fs, a1fs_blk_t n) {
    a1fs_blk_t max = fs->s->s_num_blocks;
    bool valid;
    for (a1fs_blk_t this_blk = 0; this_blk <= max - n; this_blk++) {
        valid = true;
        for (a1fs_blk_t offset = 0; offset < n; offset++) {
            if (is_used_bit(fs->image, this_blk + offset, LOOKUP_DB)) {
                valid = false; break;
            }
        }
        if (valid)
            return this_blk;
    }
    return -1;
}

/** Extend the file by specified bytes. */
int extend_by_amount(fs_ctx *fs, a1fs_inode *ino, size_t size) {
    size_t num_tailing_data_byte = ino->size % A1FS_BLOCK_SIZE;
    size_t num_tailing_blank_byte;
    if (num_tailing_data_byte == 0) {
        num_tailing_blank_byte = 0;
    } else {
        num_tailing_blank_byte = A1FS_BLOCK_SIZE - num_tailing_data_byte;
    }
    // make use of the trailing blank bytes
    a1fs_extent *last_ext = find_last_used_ext(fs->image, ino);
    if (num_tailing_blank_byte != 0) {
        a1fs_blk_t last_blk = last_ext->start + last_ext->count - 1;
        unsigned char *tailing_blank_start = (unsigned char *)jump_to(fs->image, last_blk, A1FS_BLOCK_SIZE);
        tailing_blank_start += num_tailing_data_byte;
        // format tailing blank to use
        memset(tailing_blank_start, 0, num_tailing_blank_byte);
    }
    // if the extending size is less or equal to the tailing blank, then we are done
    if (num_tailing_blank_byte >= size) return 0;
    // ------------- new blocks need to be assigned ----------------
    // calculate the number of new blocks needed after making use of tailing blanks
    a1fs_blk_t num_extend_blk = CEIL_DIV(size - num_tailing_blank_byte, A1FS_BLOCK_SIZE);
    // if the system can't store, return nospc
    if (!has_n_free_blk(fs, num_extend_blk, LOOKUP_DB)) return -ENOSPC;
    // in a loop, we store all blocks, i.e. num_extend_blk = 0
    a1fs_blk_t n = num_extend_blk;
    while (num_extend_blk) {
        // use window sliding to find the largest possible consecutive blocks
        a1fs_blk_t extent_start = window_slide(fs, n);
        if (extent_start == (a1fs_blk_t) -1) {
            // extent of this length cannot be found, try a smaller one
            n--;
            continue;
        } else {
            // we have found an extent of length n, starting at extent_start
            if (last_ext->start + last_ext->count == extent_start) {
                // extend last extent
                last_ext->count += n;
            } else {
                // ------------- need a new extent --------------
                a1fs_blk_t ext_offset = find_first_empty_extent_offset(fs->image, ino->i_ptr_extent);
                if (ext_offset == (a1fs_blk_t) -1) {
                    return -ENOSPC;
                } else {
                    // store block info in the new extent
                    a1fs_extent *new_ext = (a1fs_extent *)jump_to(fs->image, ino->i_ptr_extent, A1FS_BLOCK_SIZE);
                    new_ext += ext_offset;
                    new_ext->start = extent_start;
                    new_ext->count = n;
                    // update number of extents
                    ino->i_extents++;
                }   
            }
            // format new space
            void *start_blk = jump_to(fs->image, extent_start, A1FS_BLOCK_SIZE);
            memset(start_blk, 0, n * A1FS_BLOCK_SIZE);
            // mask used
            mask_range(fs->image, extent_start, extent_start + n, LOOKUP_DB, true);
            // update number of blocks to find
            num_extend_blk -= n;
        }
    }
    return 0;
}

/** Find until reach to the blk_offset. */
a1fs_blk_t find_blk_given_offset(void *image, a1fs_inode *file_ino, a1fs_blk_t blk_offset) {
    a1fs_extent *start_ext = (a1fs_extent *)jump_to(image, file_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    a1fs_extent *this_ext;
    // accumulate to blk_offset
    a1fs_blk_t blk_acc = 0;
    for (a1fs_blk_t offset = 0; offset < 512; offset++) {
        this_ext = start_ext + offset;
        if (this_ext->start == (a1fs_blk_t) -1) continue;
        if ((blk_acc + this_ext->count) >= blk_offset) {
            return this_ext->start + (blk_offset - blk_acc);
        } else {
            blk_acc += this_ext->count;
        }
    }
    return -1;
}

#endif
