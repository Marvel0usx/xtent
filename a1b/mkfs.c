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
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#include "a1fs.h"
#include "map.h"
#include "util.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help  = true; return true;// skip other arguments
			case 'f': opts->force = true; break;
			case 'z': opts->zero  = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
	a1fs_superblock *s = (a1fs_superblock *) image;
	int is_valid = true;
	if (s->magic != A1FS_MAGIC) {
		is_valid = false;
	} else if (IS_ZERO(s->size)) {
		is_valid = false;
	} else if (IS_ZERO(s->s_num_blocks)) {
		is_valid = false;
	} else if (IS_ZERO(s->s_num_inodes)) {
		is_valid = false;
	} else if (IS_ZERO(s->s_num_data_bitmaps)) {
		is_valid = false;
	} else if (IS_ZERO(s->s_num_inode_tables)) {
		is_valid = false;
	}
	// superblock + inode bitmap + num_data_bitmaps + inode_tables
	unsigned int num_reserved_blk = 1 + s->s_num_inode_bitmaps + s->s_num_data_bitmaps + s->s_num_inode_tables;
	if (num_reserved_blk != s->s_num_reserved_blocks) {
		is_valid = false;
	}
	unsigned int num_data_bitmaps = (uint32_t) (s->s_num_blocks + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;
	if (num_data_bitmaps != s->s_num_data_bitmaps) {
		is_valid = false;
	}
	unsigned int num_inode_tables = (uint32_t) (s->s_num_inodes * sizeof(a1fs_inode) + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;
	if (num_inode_tables != s->s_num_inode_tables) {
		is_valid = false;
	}
	if (s->s_num_blocks != s->size / A1FS_BLOCK_SIZE) {
		is_valid = false;
	}
	unsigned int num_inode_bitmaps = (uint32_t) (s->s_num_inodes + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;
	if (num_inode_bitmaps != s->s_num_inode_bitmaps) {
		is_valid = false;
	}

	// Check root
	a1fs_inode *root = (a1fs_inode *) (image + s->s_inode_table * A1FS_BLOCK_SIZE);
	if (root->mode != (S_IFDIR | 0777)) {
		is_valid = false;
	}

	return is_valid;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO: initialize the superblock and create an empty root directory
	//NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	a1fs_superblock *s = (a1fs_superblock *) image;
	s->magic = A1FS_MAGIC;
	s->size = size;
	s->s_num_blocks = size / A1FS_BLOCK_SIZE;	// this is equivalent to floor of size / 4K
	s->s_num_inodes = opts->n_inodes;
	s->s_num_inode_tables = (opts->n_inodes * sizeof(a1fs_inode) + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;
	s->s_num_inode_bitmaps = (opts->n_inodes + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;
	s->s_num_data_bitmaps = (s->s_num_blocks + A1FS_BLOCK_SIZE) / A1FS_BLOCK_SIZE;

	s->s_data_bitmap = (a1fs_blk_t) (1 + s->s_num_inode_bitmaps);
	s->s_inode_table = (a1fs_blk_t) (s->s_data_bitmap + s->s_num_data_bitmaps);
	s->s_first_block = (a1fs_blk_t) (s->s_inode_table + s->s_num_inode_tables);
	s->s_num_reserved_blocks = 1 + s->s_num_inode_bitmaps + s->s_num_data_bitmaps + s->s_num_inode_tables;
	s->s_num_free_inodes = s->s_num_inodes - 1;
	s->s_num_free_blocks = s->s_num_blocks - s->s_num_reserved_blocks;

	a1fs_inode *root = (a1fs_inode *) (image + s->s_inode_table * A1FS_BLOCK_SIZE);
	root->mode = (mode_t) (S_IFDIR | 0777);
	root->links = 2;
	root->size = 0;
    clock_gettime(CLOCK_REALTIME, &(root->mtime));
	unsigned char *bitmap = (unsigned char *) (image + s->s_inode_bitmap * A1FS_BLOCK_SIZE);
	bitmap[0] = 1;

	return false;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
