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
 * CSC369 Assignment 1 - File system runtime context implementation.
 */

#include "fs_ctx.h"


bool fs_ctx_init(fs_ctx *fs, void *image, size_t size)
{
	fs->image = image;
	fs->size = size;

	//TODO: check if the file system image can be mounted and initialize its
	// runtime state
	// record the place of the superblock
	fs->s = (a1fs_superblock *) image;

	// record the inumber of root node
	fs->root_inum = 0;

	return true;
}

void fs_ctx_destroy(fs_ctx *fs)
{
	//TODO: cleanup any resources allocated in fs_ctx_init()
	(void)fs;
}
