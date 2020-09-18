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

	/*TODO*/

	return 0;
}
