/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";

static int hello_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, hello_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
	} else
		res = -ENOENT;

	return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, hello_path + 1, NULL, 0);

	return 0;
}

static int hello_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, hello_path) != 0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
	if(strcmp(path, hello_path) != 0)
		return -ENOENT;

	len = strlen(hello_str);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		memcpy(buf, hello_str + offset, size);
	} else
		size = 0;

	return size;
}

// DEFINED CONSTANTS TO BE USED (these values may be changed in the future)
#define MAX_NUM_BLOCKS 10000
#define MAX_FILE_SIZE 1638400
#define BLOCK_SIZE 4096
#define CHAR_FILE_LENGTH 50	// the length in characters of the filesystem in the host directory; i.e.: /fuse/fusedata.X --> 0 <= X <= MAX_NUM_BLOCKS - 1

// Default path files
static const char *files_path = "/fuse";

struct jab_state {
	char *rootdir;
};
#define JAB_DATA ((struct jab_state *) fuse_get_context()->private_data)

// fuse_conn_info contains connection info that is passed to the ->init() method
// initialize fusedata blocks from fusedata.0 to fusedata.(MAX_NUM_BLOCKS-1)
// Each block is preallocated (upon FS creation) with all zeros and stored on the host file system
// fuse_conn_info contains connection info that is passed to the ->init() method
// initialize fusedata blocks from fusedata.0 to fusedata.(MAX_NUM_BLOCKS-1)
// Each block is preallocated (upon FS creation) with all zeros and stored on the host file system
void *jab_init(struct fuse_conn_info *conn)
{
	char *path = calloc(1, CHAR_FILE_LENGTH * sizeof(char));
	char i_str[20]; // used for snprintf in the for loop below
	char fusedata_str[10]; // will contain "fusedata."
	strcpy(fusedata_str, "fusedata.");
	int i;
	for(i = 0; i < MAX_NUM_BLOCKS; ++i) {
		snprintf(i_str, 20,"%d",i); // convert i to a str stored in i_str
		// itoa(i, i_str, 10); // convert i to a string stored in i_str
		strcpy(path, files_path); // re-initialize FUSE root path
		strcat(path, strcat(fusedata_str, i_str)); // path will now look like: /fuse/fusedata.X --> /fuse can be changed by changing files_path and X is an integer value
		FILE *fd = fopen(path, "r");
		if(!fd) {
			fprintf(stderr, "ERROR: could not open fusedata blocks");
			abort();
		}
		fclose(fd);
	}
	free(path); // free up allocated memory space
	return JAB_DATA;
}

static struct fuse_operations hello_oper = {
	.getattr	= hello_getattr,
	.readdir	= hello_readdir,
	.open		= hello_open,
	.read		= hello_read,
	.init		= jab_init,
};

int main(int argc, char *argv[])
{
	struct jab_state *jab_data;
	jab_data = malloc(sizeof(struct jab_state));
	if(jab_data == NULL) {
		fprintf(stderr, "ERROR: Allocation error in main");
		abort();
	}

	return fuse_main(argc, argv, &hello_oper, NULL);
}
