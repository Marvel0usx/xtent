#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ext2.h"

#define FS1
#define FS2

// Pointer to the beginning of the disk (byte 0)
static const unsigned char *disk = NULL;

static void print_blockgroup(const struct ext2_group_desc *group, int verbose)
{
	if (verbose)
	{
		printf("Block group:\n");
		printf("    block bitmap: %d\n", group->bg_block_bitmap); 
		printf("    inode bitmap: %d\n", group->bg_inode_bitmap);
		printf("    inode table: %d\n", group->bg_inode_table);
		printf("    free blocks: %d\n", group->bg_free_blocks_count);
		printf("    free inodes: %d\n", group->bg_free_inodes_count);
		printf("    used_dirs: %d\n", group->bg_used_dirs_count); 
	}
	else
	{
		printf("%d, %d, %d, %d, %d, %d\n",
			group->bg_block_bitmap,
			group->bg_inode_bitmap,
			group->bg_inode_table,
			group->bg_free_blocks_count,
			group->bg_free_inodes_count,
			group->bg_used_dirs_count);
	}
}

void print_usage()
{
	fprintf(stderr, "Usage: readimage [-tv] <image file name>\n");
	fprintf(stderr, "     -t will print the output in terse format for auto-testing\n");
	fprintf(stderr, "     -v will print the output in verbose format for easy viewing\n");
}

static void print_bitmap(unsigned char *block_start, int block_count) {
	for (int num = 0; num < block_count / 8; num++) {
		unsigned char block = *(block_start + num);
		for (int idx = 0; idx <= 7; idx++) {
			// printf("%d", block & (1 << idx));
			unsigned char bit = block & (1<< idx);
			if (bit) {
				printf("1");
			} else {
				printf("0");
			}
		}
		printf(" ");
	}
	printf("\n");
}

static int is_used(unsigned char *block_start, unsigned int idx) {
	unsigned int bitmap_group_idx = idx / 8;
	unsigned int position_in_group = idx % 8;

	unsigned char *bitmap_group = block_start + bitmap_group_idx;
	return (*bitmap_group & (1 << position_in_group)) != 0;
}

int main(int argc, char *argv[])
{
	int option;
	int verbose = 1;
	while ((option = getopt(argc, argv, "tv")) != -1)
	{
		switch (option)
		{
		case 't':
			verbose = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			print_usage();
			exit(1);
		}
	}

	if (optind >= argc)
	{
		print_usage();
		exit(1);
	}

	int fd = open(argv[optind], O_RDONLY);
	if (fd == -1)
	{
		perror("open");
		exit(1);
	}

	// Map the disk image into memory so that we don't have to do any reads and writes
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}

	const struct ext2_super_block *sb = (const struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);

	if (verbose)
	{
		printf("Inodes: %d\n", sb->s_inodes_count);
		printf("Blocks: %d\n", sb->s_blocks_count);
	}
	else
	{
		printf("%d, %d, ", sb->s_inodes_count, sb->s_blocks_count);
	}

	const struct ext2_group_desc *group = (const struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE * 2);
	print_blockgroup(group, verbose);

	unsigned char *block_start = (unsigned char *) (disk + EXT2_BLOCK_SIZE * group->bg_block_bitmap);
	unsigned char *inode_start = (unsigned char *) (disk + EXT2_BLOCK_SIZE * group->bg_inode_bitmap);
	
	if (verbose) {
		printf("Block bitmap: ");
		print_bitmap(block_start, sb->s_blocks_count);
		printf("Inode bitmap: ");
		print_bitmap(inode_start, sb->s_inodes_count);
	} else {
		print_bitmap(block_start, sb->s_blocks_count);
		print_bitmap(inode_start, sb->s_inodes_count);
	}

	if (verbose) {
		printf("Inodes:\n");
		// print root node
		struct ext2_inode *root_inode = (struct ext2_inode *) (disk + EXT2_BLOCK_SIZE * group->bg_inode_table) + EXT2_ROOT_INO ; 
		printf("[%d] ", EXT2_ROOT_INO);
		printf("type: ");
		if (root_inode->i_mode & EXT2_S_IFREG) {
			printf("f ");
		} else if (root_inode->i_mode & EXT2_S_IFDIR) {
			printf("d ");
		} else {
			printf("%d ", root_inode->i_mode);}

		printf("size: %d ", root_inode->i_size);
		printf("links: %d ", root_inode->i_links_count);
		printf("blocks: %d\n", root_inode->i_blocks);

		printf("[%d] Blocks: ", EXT2_ROOT_INO);
		for (int idx = 0; idx < root_inode->i_blocks; ) {
			unsigned int inum;
			if ((inum = root_inode->i_block[idx++]) != 0) {
				printf("%d ", inum);
			}
		};
		printf("\n");

		for (unsigned int idx = 11; idx < sb->s_inodes_count; idx++) {
			if (is_used(inode_start, idx)) {
				struct ext2_inode *this_inode = (struct ext2_inode *) (disk + EXT2_BLOCK_SIZE * group->bg_inode_table) + 11 + idx;
				printf("[%d] Blocks: ", 11 + idx);
				for (int jdx = 0; jdx < this_inode->i_blocks; ) {
					unsigned int inum;
					if ((inum = this_inode->i_block[jdx++]) != 0) {
						printf("%d ", inum);
					}
				}
			}
		}
	} else {

	}

	return 0;
}
