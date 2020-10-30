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
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"
#include "util.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	fs_ctx *fs = get_fs();
	(void) path;
	memset(st, 0, sizeof(*st));
	st->f_bsize   = A1FS_BLOCK_SIZE;
	st->f_frsize  = A1FS_BLOCK_SIZE;
	// fill in the rest of required fields based on the information stored
	// in the superblock
	st->f_namemax = A1FS_NAME_MAX;
	// total number of blocks
	st->f_blocks = fs->s->s_num_blocks;
	// number of free blocks
	st->f_bfree = fs->s->s_num_free_blocks;
	st->f_bavail = st->f_bfree;
	// total number of inodes
	st->f_files = fs->s->s_num_inodes;
	// number of free inodes
	st->f_ffree = fs->s->s_num_free_inodes;
	st->f_favail = st->f_ffree;
	// maximun filename length
	st->f_namemax = A1FS_NAME_MAX;

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	// lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
	int err = path_lookup(path, fs);
	if (err < 0) {
		return err;
	} else {
		a1fs_inode *this_file = get_inode_by_inumber(fs->image, err);
		if (this_file == NULL) perror("Invalid inode!");
		st->st_mode = this_file->mode;
		st->st_nlink = this_file->links;
		st->st_blocks = CEIL_DIV(this_file->size, 512);
		st->st_mtime = (time_t) this_file->mtime.tv_sec;
	}
	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	// lookup the directory inode for given path and iterate through its
	// directory entries
	int err = path_lookup(path, fs);
	a1fs_inode *dir_ino = get_inode_by_inumber(fs->image, (a1fs_ino_t) err);
	a1fs_extent *this_extent = (a1fs_extent *) jump_to(fs->image, dir_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
    for (a1fs_blk_t extent_offset = 0; extent_offset < 512; extent_offset++) {
        if ((this_extent + extent_offset)->start == (a1fs_blk_t) -1) continue;
		if ((this_extent + extent_offset)->start >= fs->s->s_num_blocks) continue;
        a1fs_dentry *this_dentry;
        for (a1fs_blk_t blk_offset = 0; blk_offset < (this_extent + extent_offset)->count; blk_offset++) {
            uint32_t blk_num = (this_extent + extent_offset)->start + blk_offset;
            this_dentry = (a1fs_dentry *) jump_to(fs->image, blk_num, A1FS_BLOCK_SIZE);
            for (uint32_t dentry_offset = 0; dentry_offset < 16; dentry_offset++ ) {
				a1fs_ino_t inum = (this_dentry + dentry_offset)->ino;
				if (inum == (a1fs_ino_t) -1) continue;
				else {
     	            err = filler(buf, (this_dentry + dentry_offset)->name, NULL, 0);
					if (err != 0) {
						return -ENOMEM;
					} 
				}
            }
        }
    }
	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 * 	
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

    char *parent = strdup(path);
    char *name = strdup(path);
	char *parent_to_free = parent;
	char *name_to_free = name;

    if (parent[strlen(parent)-1] == '/') {
        parent[strlen(parent)-1] = '\0';
   		name[strlen(name)-1] = '\0';
    }

    name = strrchr(name, '/'); name++;
    parent[strlen(parent) - strlen(name)] = '\0';

	a1fs_ino_t inum = path_lookup(parent, fs);
	if (fs->s->s_num_free_inodes < 1) return -ENOSPC;
	a1fs_inode *this_inode = get_inode_by_inumber(fs->image, inum);
	a1fs_dentry *free_dentry = find_first_free_dentry(fs->image, inum);
	a1fs_extent *free_extent;
	if (free_dentry == NULL) {
		if (has_n_free_blk(fs, 3, LOOKUP_DB)) {
			int ext_offset = find_first_empty_extent_offset(fs->image, this_inode->i_ptr_extent);
			if (ext_offset == -1) {
				goto err;
			} else {
				free_extent = (a1fs_extent *) jump_to(fs->image, this_inode->i_ptr_extent, A1FS_BLOCK_SIZE);
				free_extent += ext_offset;
				a1fs_blk_t new_dentry_blk_num = find_first_free_blk_num(fs->image, LOOKUP_DB);
				init_directory_blk(fs->image, new_dentry_blk_num);
				mask(fs->image, new_dentry_blk_num, LOOKUP_DB, true);
				free_extent->start = new_dentry_blk_num;
				free_extent->count = 1;
				free_dentry = (a1fs_dentry *) jump_to(fs->image, new_dentry_blk_num, A1FS_BLOCK_SIZE);
			}
		} else {
			goto err;
		}
	} else {
		if (!has_n_free_blk(fs, 2, LOOKUP_DB)) {
			goto err;
		}
	}
	if (!has_n_free_blk(fs, 1, LOOKUP_IB)) goto err;
	create_new_dir_in_dentry(fs->image, free_dentry, name, mode);
	// increment link of parent inode
	this_inode->links++;

err:
	free(parent_to_free);
	free(name_to_free);

	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	// prepare parent path and filename string
	char *parent = strdup(path);
    char *name = strdup(path);
	char *parent_to_free = parent;
	char *name_to_free = name;

    if (parent[strlen(parent)-1] == '/') {
        parent[strlen(parent)-1] = '\0';
   		name[strlen(name)-1] = '\0';
    }

    name = strrchr(name, '/'); name++;
    parent[strlen(parent) - strlen(name)] = '\0';

	// find the inode of parent
	a1fs_ino_t parent_inum = path_lookup(parent, fs);
	a1fs_inode *parent_ino = get_inode_by_inumber(fs->image, parent_inum);
	// find the dentry of file to be deleted
	a1fs_dentry *dentry_rm = find_dentry_in_dir(fs->image, parent_ino, name);
	// check if the dir is empty
	a1fs_inode *ino_rm = get_inode_by_inumber(fs->image, dentry_rm->ino);
	if (is_empty_dir(fs->image, ino_rm)) {
		// free the dentry block of this inode
		free_dentry_blks(fs->image, ino_rm);
		// free the extent block of this inode
		free_extent_blk(fs->image, ino_rm);
		dentry_rm->ino = (a1fs_ino_t) -1;
		parent_ino->links--;
		free(parent_to_free);
		free(name_to_free);
		return 0;
	} else {
		free(parent_to_free);
		free(name_to_free);
		return -ENOTEMPTY;
	}	
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	// at least one inode
	if (!has_n_free_blk(fs, 1, LOOKUP_IB)) return -ENOSPC;

    // prepare parent path and filename string
	char *parent = strdup(path);
    char *name = strdup(path);
	char *parent_to_free = parent;
	char *name_to_free = name;

    if (parent[strlen(parent)-1] == '/') {
        parent[strlen(parent)-1] = '\0';
   		name[strlen(name)-1] = '\0';
    }

    name = strrchr(name, '/'); name++;
    parent[strlen(parent) - strlen(name)] = '\0';

	// prepare parent directory to store new file
	a1fs_ino_t parent_inum = path_lookup(parent, fs);
	a1fs_inode *parent_ino = get_inode_by_inumber(fs->image, parent_inum);
	a1fs_dentry *parent_dentry = find_first_free_dentry(fs->image, parent_inum);
	// create new block of dentries
	if (parent_dentry == NULL) {
		a1fs_extent *parent_free_ext;
		if (has_n_free_blk(fs, 2, LOOKUP_DB)) {
			int free_ext_offset = find_first_empty_extent_offset(fs->image, parent_ino->i_ptr_extent);
			// excess 512 extents
			if (free_ext_offset == -1) {
				goto err;
			} else {
				// use new extent to store new dentry block
				parent_free_ext = (a1fs_extent *) jump_to(fs->image, parent_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
				parent_free_ext += free_ext_offset;
				// init new dentry block
				a1fs_blk_t new_dentry_blk_num = find_first_free_blk_num(fs->image, LOOKUP_DB);
				init_directory_blk(fs->image, new_dentry_blk_num);
				mask(fs->image, new_dentry_blk_num, LOOKUP_DB, true);
				// record new dentry block
				parent_free_ext->start = new_dentry_blk_num;
				parent_free_ext->count = 1;
				// refer to the newly created dentry
				parent_dentry = (a1fs_dentry *) jump_to(fs->image, new_dentry_blk_num, A1FS_BLOCK_SIZE);
			}
		} else {
			goto err;
		}
	} else {
		// need 1 free data block to store extent
		if (!has_n_free_blk(fs, 1, LOOKUP_DB)) {
			goto err;
		}
	}
	// create new file after preparation
	create_new_file_in_dentry(fs->image, parent_dentry, name, mode);

err:
	free(parent_to_free);
	free(name_to_free);
	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	// prepare parent path and filename string
	char *parent = strdup(path);
    char *name = strdup(path);
	char *parent_to_free = parent;
	char *name_to_free = name;
	// split path to parent dir and file
    if (parent[strlen(parent)-1] == '/') {
        parent[strlen(parent)-1] = '\0';
   		name[strlen(name)-1] = '\0';
    }
    name = strrchr(name, '/'); name++;
    parent[strlen(parent) - strlen(name)] = '\0';
	// prepare to remove file
	a1fs_ino_t parent_inum = path_lookup(parent, fs);
	a1fs_inode *parent_ino = get_inode_by_inumber(fs->image, parent_inum);
	a1fs_ino_t file_inum = path_lookup(path, fs);
	a1fs_inode *file_ino = get_inode_by_inumber(fs->image, file_inum);
	// free dentry containing the file
	a1fs_dentry *parent_dentry = find_dentry_in_dir(fs->image, parent_ino, name);
	free_dentry_blks(fs->image, file_ino);
	// free file extent
	free_extent_blk(fs->image, file_ino);
	// free inode
	mask(fs->image, file_inum, LOOKUP_IB, false);
	// remove from dentry
	parent_dentry->ino = (a1fs_ino_t) -1;
	free(parent_to_free);
	free(name_to_free);
	return 0;
}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	a1fs_ino_t file_inum = path_lookup(path, fs);
	a1fs_inode *file_ino = get_inode_by_inumber(fs->image, file_inum);

	// set to current time if new timestemp is not specified
	if (times == NULL) {
		clock_gettime(CLOCK_REALTIME, &(file_ino->mtime));	
	} else {
		memcpy(&file_ino->mtime, &times[1], sizeof(struct timespec));
	}
	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	assert(size >= 0);

	// find inode of file
	a1fs_ino_t file_inum = path_lookup(path, fs);
	a1fs_inode *file_ino = get_inode_by_inumber(fs->image, file_inum);

	// set new file size, possibly "zeroing out" the uninitialized range
	int size_delta = file_ino->size - size;
	if (size_delta == 0) return 0;
	
	a1fs_extent *last_ext = find_last_used_ext(fs->image, file_ino);
    // this file is newly created if there is no used extent, and we have to create a new
	if (!last_ext) {
		last_ext = (a1fs_extent *)jump_to(fs->image, file_ino->i_ptr_extent, A1FS_BLOCK_SIZE);
		a1fs_blk_t new_blk = find_first_free_blk_num(fs->image, LOOKUP_DB);
		if (new_blk == (a1fs_blk_t) -1) return -ENOSPC;
		last_ext->start = new_blk;
		last_ext->count = 1;
		mask(fs->image, new_blk, LOOKUP_DB, true);
	}

	int err;
	if (size_delta > 0) {
		err = shrink_by_amount(fs->image, file_ino, size_delta);
	} else {
		err = extend_by_amount(fs, file_ino, -size_delta);
	}
	if (err == 0) {
		file_ino->size = size;
		clock_gettime(CLOCK_REALTIME, &(file_ino->mtime));
	}
	return err;
}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fs;
	return -ENOSYS;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
