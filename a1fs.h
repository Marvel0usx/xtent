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
 * CSC369 Assignment 1 - a1fs types, constants, and data structures header file.
 */

#pragma once

#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>


/**
 * a1fs block size in bytes. You are not allowed to change this value.
 *
 * The block size is the unit of space allocation. Each file (and directory)
 * must occupy an integral number of blocks. Each of the file systems metadata
 * partitions, e.g. superblock, inode/block bitmaps, inode table (but not an
 * individual inode) must also occupy an integral number of blocks.
 */
#define A1FS_BLOCK_SIZE 4096

/** Block number (block pointer) type. */
typedef uint32_t a1fs_blk_t;

/** Inode number type. */
typedef uint32_t a1fs_ino_t;


/** Magic value that can be used to identify an a1fs image. */
#define A1FS_MAGIC 0xC5C369A1C5C369A1ul

/** a1fs superblock. */
typedef struct a1fs_superblock {
	/** Must match A1FS_MAGIC. */
	uint64_t magic;
	/** File system size in bytes. */
	uint64_t size;

	//TODO: add necessary fields
	/** Number of data blocks. */
	uint32_t s_num_blocks;
	/** Number of inodes. */
	uint32_t s_num_inodes;
	/** Number of inode bitmaps. */
	uint16_t s_num_inode_bitmaps;
	/** Number of data bitmaps in contiguous blocks. */
	uint16_t s_num_data_bitmaps;
	/** Number of inode tables. */
	uint16_t s_num_inode_tables;
	/** Block number of the first inode bitmap. */
	a1fs_blk_t s_inode_bitmap;
	/** Block number of the first data bitmap. */
	a1fs_blk_t s_data_bitmap;
	/** Block number of the first inode table. */
	a1fs_blk_t s_inode_table;
	/** Block number of the first data block. */
	a1fs_blk_t s_first_block;
	/** Number of reserved blocks. */
	uint32_t s_num_reserved_blocks;
	/** Number of free inodes. */
	uint32_t s_num_free_inodes;
	/** Number of free data blocks. */
	uint32_t s_num_free_blocks;

} a1fs_superblock;

// Superblock must fit into a single block
static_assert(sizeof(a1fs_superblock) <= A1FS_BLOCK_SIZE,
              "superblock is too large");


/** Extent - a contiguous range of blocks. */
typedef struct a1fs_extent {
	/** Starting block of the extent. */
	a1fs_blk_t start;
	/** Number of blocks in the extent. */
	a1fs_blk_t count;

} a1fs_extent;


/** a1fs inode. */
typedef struct a1fs_inode {
	/** File mode. */
	mode_t mode;

	/**
	 * Reference count (number of hard links).
	 *
	 * Each file is referenced by its parent directory. Each directory is
	 * referenced by its parent directory, itself (via "."), and each
	 * subdirectory (via ".."). The "parent directory" of the root directory is
	 * the root directory itself.
	 */
	uint32_t links;

	/** File size in bytes. */
	uint64_t size;

	/**
	 * Last modification timestamp.
	 *
	 * Must be updated when the file (or directory) is created, written to, or
	 * its size changes. Use the clock_gettime() function from time.h with the
	 * CLOCK_REALTIME clock; see "man 3 clock_gettime" for details.
	 */
	struct timespec mtime;

	//TODO: add necessary fields

	/** Total number of extents. */
	uint32_t i_extents;
	/** Block number of an array of extents. */
	a1fs_blk_t i_ptr_extent;

	//NOTE: You might have to add padding (e.g. a dummy char array field) at the
	// end of the struct in order to satisfy the assertion below. Try to keep
	// the size of this struct minimal, but don't worry about the "wasted space"
	// introduced by the required padding.
	char extra[24];

} a1fs_inode;

// A single block must fit an integral number of inodes
static_assert(A1FS_BLOCK_SIZE % sizeof(a1fs_inode) == 0, "invalid inode size");


/** Maximum file name (path component) length. Includes the null terminator. */
#define A1FS_NAME_MAX 252

/** Maximum file path length. Includes the null terminator. */
#define A1FS_PATH_MAX PATH_MAX

/** Fixed size directory entry structure. */
typedef struct a1fs_dentry {
	/** Inode number. */
	a1fs_ino_t ino;
	/** File name. A null-terminated string. */
	char name[A1FS_NAME_MAX];

} a1fs_dentry;

static_assert(sizeof(a1fs_dentry) == 256, "invalid dentry size");
