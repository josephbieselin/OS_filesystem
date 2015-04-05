/*
	Joseph Bieselin
	Operating Systems -- CS-UY 3224
	HW 2 -- filesystem
	
	Filesystem compilation and testing
	jabFS.c
	gcc -Wall -g ~/OS/OS_filesystem/rewrite_hello.c `pkg-config fuse --cflags --libs` -o jabFS;
	./jabFS ./mount;
	ls -l mount
*/

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>	// file control options
#include <assert.h>	// useful for debugging


// DEFINED CONSTANTS TO BE USED (these values may be changed in the future)
#define MAX_NUM_BLOCKS 10000
#define MAX_FILE_SIZE 1638400
#define BLOCK_SIZE 4096
#define MAX_PATH 100	// the length in characters of the filesystem in the host directory; i.e.: /fuse/fusedata.X --> 0 <= X <= MAX_NUM_BLOCKS - 1

/*// Default path files
static const char *files_path = "/tmp/fuse";
*/



static const char *hello_str = "Hello World!\n";
static const char *hello_path = "/hello";


// concats "fusedata.X" to the passed in string
char *fusedata_concat_X(char* str, int num)
{
	
	char *block_num_str = malloc(15); // Up to 15 digits can be used to represent block numbers
	sprintf(block_num_str, "%d", MAX_NUM_BLOCKS); // block_num_str will hold the string "20000" if 20000 blocks are to be created for this filesystem
	char *i_str = malloc(strlen(block_num_str) + 1); // + 1 for NULL character
	sprintf(i_str, "%d", num); // convert num to a string stored in i_str
	strcat(str, "fusedata.");
	strcat(str, i_str);

	free(block_num_str); free(i_str);
	return str;
}

// --------------------------- FILESYSTEM FUNCTIONS ---------------------------- //

static int hello_getattr(const char *path, struct stat *stbuf)
{
	/*
	stat.h:
	-------
	ino_t	st_ino	= file serial number
	mode_t	st_mode = mode of file
	nlink_t	st_nlink = number of links to the file
	uid_t	st_uid = user ID of file
	gid_t	st_gid = group ID of file
	off_t	st_size = file size in bytes (if file is a regular file)
	time_t	st_atime = time of last access
	time_t	st_mtime = time of last data modification
	time_t	st_ctime = time of last status change
	blksize_t st_blksize = a filesystem-specific preferred I/O block size for this object
	blkcnt_t  st_blocks = number of blocks allocated for this object
	*/
	int res = 0;
	char fuse_chars[10];
	// Set the memory fields of stat to zero because the code below will fill in the stat buffer fields to be displayed
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, hello_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
	} else if (strcmp("fusedata.", strncpy(fuse_chars, path, 9)) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		// HAVE TO GET ACTUAL SIZE OF THE FILE
		stbuf->st_size = 0;
	} else
		res = -ENOENT;

	return res;
}

static int hello_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
	int i;
	// char fusedata_str[25];
	char *fusedata_str;

	if (strcmp(path, "/") != 0)
		return -ENOENT;
	
	// filler tells which entries to show in readdir
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, hello_path + 1, NULL, 0);
	for(i = 0; i < MAX_NUM_BLOCKS; ++i) {
		fusedata_str = fusedata_concat_X(i);
		filler(buf, fusedata_str + 1, NULL, 0);
	}

	return 0;
}


static int hello_open(const char *path, struct fuse_file_info *fi)
{
	/*
	fi->flags:
	----------
	EACCES = file exists but is not readable/writable as requested by the flags argument | the file does not exist and the directory is unwritable so it cannot be created
	EEXIST = both O_CREATE and O_EXCL are set, and the named file already exists
	EINTR = the open operation was interrupted by a signal
	EISDIR = the flags argument specified write access, and the file is a directory
	ENOENT = the named file does not exist, and O_CREAT is not specified
	EROFS = the file resides on a read-only file system and any of O_WRONLY, O_RDWR, and O_TRUNC are set in the flags argument, or O_CREAT is set and the file does not already exist

	File Access Modes:
	------------------
	O_RDONLY = open the file for read access
	O_WRONLY = open the file for write access
	O_RDWR 	 = open the file for both reading and writing
	O_EXCL	 = exclusive use flag
	O_TRUNC	 = truncate flag
	O_CREATE = create file if it does not exist
	*/
	// PROVIDE FUNCTIONALITY FOR FULL PATH'S OF FILES
	int i;
	int boolean = 0;
	// char fusedata_str[25];
	char *fusedata_str;

	if (strcmp(path, hello_path) != 0)
		return -ENOENT;

	for(i = 0; i < MAX_NUM_BLOCKS; ++i) {
		fusedata_str = fusedata_concat_X(i);
		if (strcmp(path, fusedata_str) == 0)
			boolean = 1;
	}
	if (boolean == 0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;
	
	int fd = open(path, fi->flags);
	if (fd < 0)
		return -errno;
	
	fi->fh = fd; // update the file handle

	return 0;
}

static int hello_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	// ADD FUNCTIONALITY TO TEST REALPATH OF FILES
	size_t len;
	(void) fi;
	int i;
	int boolean = 0;
	char *fusedata_str;
	
	// test to make sure the file exists
	if(strcmp(path, hello_path) != 0)
		return -ENOENT;

	for(i = 0; i < MAX_NUM_BLOCKS; ++i) {
		fusedata_str = fusedata_concat_X(i);
		if (strcmp(path, fusedata_str) == 0) {
			boolean = 1;
			break;
		}
	}
	if (boolean == 0)
		return -ENOENT;

	/*
	if(strcmp(path, "/testing")==0){
		len = strlen(KATZ_strz);
	} else {
		len = strlen(KATZ_str);
	}
	*/
	len = strlen(hello_str);
	if (offset < len) {
		if (offset + size > len)
			// if the offset + the size of bytes you want to read is greater than the length of the file, make the size to read equal to the starting point due to offset to the end of the file
			size = len - offset;
		memcpy(buf, hello_str + offset, size);
	} else
		size = 0;

	return size;
}

// fuse_conn_info contains connection info that is passed to the ->init() method
// initialize fusedata blocks from fusedata.0 to fusedata.(MAX_NUM_BLOCKS-1)
// Each block is preallocated (upon filesystem creation) with all zeros and stored on the host file system
static void *jab_init(struct fuse_conn_info *conn)
{
	/*
	//char fusedata_str[25]; // will contain "fusedata."
	int i;
	char *file_init; // will be used to initialize empty files of 4096 bytes
	char *file_name;

	for(i = 0; i < MAX_NUM_BLOCKS; ++i) {
		file_init = calloc(1, BLOCK_SIZE); // initialize 1 new file and memory space of 4096 if BLOCK_SIZE = 4096
		memset(file_init, '0', BLOCK_SIZE); // each block is preallocated (upon FS creation) with all zeros
		file_name = fusedata_concat_X(i);
		// char *abs_path = realpath(path, fusedata_str);
		FILE *fd = fopen(file_name, "r+");
		fwrite(file_init, BLOCK_SIZE, sizeof(char), fd);
		if(!fd) {
			fprintf(stderr, "ERROR: could not open fusedata blocks");
			abort();
		}
		fclose(fd);
	}
	// Don't free up file_init memory space because that's where the file data is stored
	*/

	int letter = '0';
	char *buf;
	buf=(char *) malloc(BLOCK_SIZE);
	memset(buf, letter, BLOCK_SIZE);
	int file_nums = MAX_NUM_BLOCKS;
	file_nums--;
	char *num_digits;
	num_digits = malloc(100);
	sprintf(num_digits, "%d", file_nums);
	char *letter_ch;
	letter_ch = (char *) malloc(strlen(num_digits));
	char *name;
	name = (char *) malloc(255*4*4);
	int i;
	for(i = 0; i < 1; i++) {
		sprintf(letter_ch, "%d", i);
		strcpy(name, fuse_get_context()->private_data);
		strcat(name, "fusedata.");
		strcat(name, letter_ch);
		FILE *fd = fopen(name, "r+");
		fwrite(buf, BLOCK_SIZE, sizeof(char), fd);
		fclose(fd);
	}
	free(letter_ch);
	free(name);
	return NULL; // RETURN JAB_DATA
}

static struct fuse_operations hello_oper = {
	.getattr	= hello_getattr,
	.readdir	= hello_readdir,
	.open		= hello_open,
	.read		= hello_read,
	.init		= jab_init,
};

// gets passed a compiled file and a place to store files
int main(int argc, char *argv[])
{
	struct jab_state *jab_data;
	jab_data = malloc(sizeof(struct jab_state));
	if(jab_data == NULL) {
		fprintf(stderr, "ERROR: Allocation error in main");
		abort();
	}

	char *rootdir_FS = realpath(argv[argc-1], NULL);
	return fuse_main(argc, argv, &hello_oper, rootdir_FS);
}
