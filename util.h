/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2020 Karen Reid
 */

/**
 * CSC369 Assignment 1 - Miscellaneous utility functions.
 */

#pragma once

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include "a1fs.h"
#include "fs_ctx.h"

#ifndef DEBUG
#define DEBUG
#endif

#define IS_ZERO(x) ((x) == 0)
#define NOT_ZERO(x) ((x) != 0)
#define LOOKUP_IB 233
#define LOOKUP_DB 666

#define CEIL_DIV(a, b) (((a) / (b)) + (((a) % (b)) != 0))

/** Check if x is a power of 2. */
static inline bool is_powerof2(size_t x)
{
	return (x & (x - 1)) == 0;
}

/** Check if x is a multiple of alignment (which must be a power of 2). */
static inline bool is_aligned(size_t x, size_t alignment)
{
	assert(is_powerof2(alignment));
	return (x & (alignment - 1)) == 0;
}

/** Align x up to a multiple of alignment (which must be a power of 2). */
static inline size_t align_up(size_t x, size_t alignment)
{
	assert(is_powerof2(alignment));
	return (x + alignment - 1) & (~alignment + 1);
}

/** Get address of block at the given block number. */
void *jump_to(void *image, uint32_t idx, size_t unit);

/** Set all bit to 0 in the given bitmap. */
void reset_bitmap(unsigned char *bitmap);

/** Get the superblock. */
a1fs_superblock *get_superblock(void *image);

/** Get the first inode bitmap. */
unsigned char *get_first_inode_bitmap(void *image);

/** Get the first data bitmap. */
unsigned char *get_first_data_bitmap(void *image);

// locate which bitmap stores this bit
a1fs_blk_t get_block_offset(uint32_t bit);

// locate which byte stores this bit
uint32_t get_byte_offset(uint32_t bit);

// locate the bit in byte
uint32_t get_bit_offset(uint32_t bit);

/** Check if the bit is used. */
bool is_used_bit(void *image, uint32_t bit, uint32_t lookup);

/** Set the bit to on in the bitmap indicated by lookup. */
void mask(void *image, uint32_t bit, uint32_t lookup, bool on);

/** Mask from start to end (exclusive) in bitmap. */
void mask_range(void *image, uint32_t offset_start, uint32_t offset_end, uint32_t lookup, bool on);

/** Find the first unused bit. Return -1 if no free block found. */
int find_first_free_blk_num(void *image, uint32_t lookup);

/** Initialize empty directory block. */
void init_directory_blk(void *image, a1fs_blk_t blk_num);

/** Find first unused directory entry in the block given its block number. 
 * return the offset of the directory, -1 for excess the max: 512.
 */
int find_first_empty_direntry_offset(void *image, a1fs_blk_t blk_num);

uint32_t get_itable_block_offset(a1fs_ino_t inum);

uint32_t get_itable_offset(a1fs_ino_t inum);

/** Get inode by inum. */
a1fs_inode *get_inode_by_inumber(void *image, a1fs_ino_t inum);

/** Format the block to empty extents. */
void init_extent_blk(void *image, a1fs_blk_t blk_num);

/* Find the first empty extent. Return the offset to the extent that is not used. If 
 * no empty extent found, return -1.
*/
int find_first_empty_extent_offset(void *image, a1fs_blk_t blk_num);

/** Find the inumber of the file given its name, starting from dir. 
 * Return -1 if not found.
 */
int find_file_ino_in_dir(void *image, a1fs_inode *dir_ino, char *name);

/* Returns the inode number for the element at the end of the path
 * if it exists.  If there is any error, return -1.
 * Possible errors include:
 *   - The path is not an absolute path
 *   - An element on the path cannot be found
 */
int path_lookup(const char *path, fs_ctx *fs);

/** Find first usable directory entry. Return NULL if no empty dentry => either abort or extend
 * existing extent.
*/
a1fs_dentry *find_first_free_dentry(void *image, a1fs_ino_t inum);

/** Initialize inode. */
void init_inode(void *image, a1fs_ino_t inum, mode_t mode, 
uint32_t links, uint64_t size, uint32_t extents, a1fs_blk_t ptr_extent);

/** Create new dir in dentry. */
void create_new_dir_in_dentry(void *image, a1fs_dentry *parent_dir, 
const char *name, mode_t mode);

static inline bool has_n_free_blk(fs_ctx *fs, a1fs_blk_t n, uint32_t lookup) {
	if (lookup == LOOKUP_DB) {
		return (bool) (fs->s->s_num_free_blocks >= n);
	} else {
		return (bool) (fs->s->s_num_free_inodes >= n);
	}
	return false;
}

/** Traverse all extent of dir_ino to find name; return the inum of the file. */
a1fs_dentry *find_dentry_in_dir(void *image, a1fs_inode *dir_ino, const char *name);

/** Return true if all associated dentry is empty, else false. */
bool is_empty_dir(void *image, a1fs_inode *dir_ino);

/** Find all blk num of dentry blk associated with ino, mask the blk in bitmap as 0. */
void free_dentry_blks(void *image, a1fs_inode *dir_ino);

/** Mask 0 the extent block. */
static inline void free_extent_blk(void *image, a1fs_inode *ino_rm) {
	mask(image, ino_rm->i_ptr_extent, LOOKUP_DB, false);
}

/** Create an empty file inside the directory. */
void create_new_file_in_dentry(void *image, a1fs_dentry *dir, const char *name, mode_t mode);

/** Find the last used extent. */
a1fs_extent *find_last_used_ext(void *image, a1fs_inode *ino);

/** Shrink the extent by n block. Mask off blocks and unset extent if 
 * the extent is empty. Return the number of extent reduced. */
int shrink_ext_by_num_blk(void *image, a1fs_extent *ext, a1fs_blk_t *num);

/** Shrink the block to the given size in byte. */
void shrink_blk_to_size(void *image, a1fs_blk_t blk_num, size_t size);

/** Shrink the file by num of block. */
void shrink_by_num_blk(void *image, a1fs_inode *ino, a1fs_blk_t num_blk);

/** Shrink amount of bytes specified in size. */
int shrink_by_amount(void *image, a1fs_inode *ino, size_t size);

/** Extend the file by specified bytes. */
int extend_by_amount(fs_ctx *fs, a1fs_inode *ino, size_t size);

/** Find until reach to the blk_offset. */
a1fs_blk_t find_blk_given_offset(void *image, a1fs_inode *file_ino, a1fs_blk_t blk_offset);

#ifdef DEBUG

#include <stdio.h>
/** Print bitmap of A1FS_BLOCK_SIZE. */
static inline void print_bitmap(unsigned char *block_start, uint32_t size) {
	for (uint32_t num = 0; num < size / 8; num++) {
		unsigned char block = *(block_start + num);
		for (uint32_t idx = 0; idx <= 7; idx++) {
			// printf("%d", block & (1 << idx));
			unsigned char bit = (block & (1 << idx)) != 0;
			printf("%d", bit);
		}
		printf(" ");
	}
	printf("\n");
}
#endif

