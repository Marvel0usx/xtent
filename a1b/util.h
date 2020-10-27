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

#define IS_ZERO(x) ((x) == 0)
#define NOT_ZERO(x) ((x) != 0)
#define LOOKUP_IB 233
#define LOOKUP_DB 666

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
static void *jump_to(void *image, uint32_t idx, size_t unit);

/** Set all bit to 0 in the given bitmap. */
static void reset_bitmap(unsigned char* bitmap);

/** Get the superblock. */
static a1fs_superblock* get_superblock(void *image);

/** Get the first inode bitmap. */
static unsigned char *get_first_inode_bitmap(void *image);

/** Get the first data bitmap. */
static unsigned char *get_first_data_bitmap(void *image);

// locate which bitmap stores this bit
static a1fs_blk_t get_block_offset(uint32_t bit);

// locate which byte stores this bit
static uint32_t get_byte_offset(uint32_t bit);

// locate the bit in byte
static uint32_t get_bit_offset(uint32_t bit);

/** Check if the bit is used. */
static bool is_used_bit(void *image, uint32_t bit, uint32_t lookup);

static void _mask(unsigned char *bitmap, uint32_t bit);

/** Set the bit to 1 in the bitmap indicated by lookup. */
static void mask(void *image, uint32_t bit, uint32_t lookup);

/** Mask from start to end (exclusive) in bitmap. */
static void mask_range(void *image, uint32_t offset_start, uint32_t offset_end, uint32_t lookup);

/** Find the first unused bit. Return -1 if no free block found. */
static uint32_t find_first_free_blk_num(void *image, uint32_t lookup);

/** Initialize empty directory block. */
static void init_directory_blk(void *image, a1fs_blk_t blk_num);

/** Find first unused directory entry in the block given its block number. 
 * return the offset of the directory, -1 for excess the max: 512.
*/
static uint32_t find_first_empty_direntry(void *image, a1fs_blk_t blk_num);

static uint32_t get_itable_block_offset(a1fs_ino_t inum);

static uint32_t get_itable_offset(a1fs_ino_t inum);

/** Get inode by inum. */
static a1fs_inode *get_inode_by_inumber(void *image, a1fs_ino_t inum);
