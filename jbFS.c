/*
  FUSE: Filesystem in Userspace

  Operating Systems - CS 3224
  HW 2 - Filesystem
  * Initialized from fusemp_fh.c
  * Programmer: Joseph Bieselin
	
  gcc -Wall jbFS.c `pkg-config fuse --cflags --libs` -o jbFS
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse.h>
//#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>


// Filesystem Constants (these can be changed later)
#define MAX_NUM_BLOCKS 	10000	// number of blocks in the filesystem
#define MAX_FILE_SIZE 	1638400	// max size in bytes of a file
#define BLOCK_SIZE 		4096	// size in bytes of a block
#define MAX_BLOCK_DIGITS	10	// the number of digits in MAX_NUM_BLOCKS won't exceed MAX_BLOCK_DIGITS-1
#define MAX_PATH_LENGTH		100	// the number of chars a file's pathname could be
#define FREE_START		1		// start of the free block list
#define FREE_END		25		// end of the free block list (must be enough blocks between start and end to hold values representing MAX_NUM_BLOCKS)
#define ROOT			26		// block for root dir
#define BLOCKS_IN_FREE	400		// number of free blocks to store in the free block list files on filesystem creation
#define UID				1		// user id (does not matter too much for this filesystem)
#define GID				1		// group id (does not matter too much for this filesystem)
#define DIR_UID			1000	// user id for a directory
#define DIR_GID			1000	// group id for a directory


// Other Constants
#define FILES_DIR "/fusedata"	// directory to put fusedata.X files
#define LOG_FILE "/home/joe/OS/filesystem/log.txt"	// log file


static const char *KATZ_str ="hi";
static const char *KATZ_path = "/KATZz";


void logmsg(const char *s)
{
	FILE *f = fopen(LOG_FILE, "a");
	fprintf(f, "%s\n\n", s); 
	fclose(f);
}


// Return true if the passed in cstring is a file that exists; false otherwise
int file_exists(const char* name)
{
	struct stat buf;
	int status;
	status = stat(name, &buf);
	return status == 0;
}

// takes in a buffer with data already in it and writes it to the file stream, returning a non-zero error value if the buffer contents were not fully written
static int write_to_file(char *buf, FILE *file_stream)
{
	// write 1 entry of strlen(buf) size bytes from buf to the file_stream (if 1 entry of that size bytes was not written, there was an error)
	if ( fwrite(buf, strlen(buf), 1, file_stream) != 1 ) {
		logmsg("FAILURE:\twrite_to_file\tfwrite");
		return -errno;
	}
	return 0;
}

/* count the number of times ch appears in the string
 * Function was created by stackoverflow user 'Jon' and can be found at the URL below:
 * http://stackoverflow.com/questions/7349053/counting-the-number-of-times-a-character-occurs-in-a-string-in-c
*/
int count_chars(const char* string, char ch)
{
    int count = 0;
    for(; *string; count += (*string++ == ch)) ;
    return count;
}

// adds the free block number back to the appropriate free block list file and returns 0 upon success
static int add_free_block(unsigned int free_block_num)
{
	// get the free block file number that the free block should go back into and open that file
	unsigned int free_block_file_num = free_block_num / BLOCKS_IN_FREE;
	free_block_file_num++;
	// open the fusedata.X file associated with the free block number passed
	char *fusedata_str = (char *) calloc(1, MAX_PATH_LENGTH);
	sprintf(fusedata_str, "fusedata.%u", free_block_file_num);
	FILE *free_block_file = fopen(fusedata_str, "r+");
	// set up some variables
	char *temp = (char *) calloc(1, BLOCK_SIZE);
	char *buf;  // = (char *) calloc(1, BLOCK_SIZE); // free(buf) not needed because strtok passes back a pointer to the contents of the string being tokenized
	// get the entire contents of the free block file into a temp variable
	fread(temp, BLOCK_SIZE, 1, free_block_file);
	rewind(free_block_file);
	// tokenize the file contents by commas
	buf = strtok(temp, ",");
	// if the file was all 0's, just add the free_block_num along with a comma to the beginning
	if (strlen(buf) == BLOCK_SIZE) {
		char *free_block_num_str = (char *) calloc(1, BLOCK_SIZE);
		sprintf(free_block_num_str, "%u,", free_block_num);
		if ( write_to_file(free_block_num_str, free_block_file) != 0) {
			free(temp); free(free_block_num_str); free(fusedata_str); fclose(free_block_file);
			printf("FAILURE:\tadd_free_block\tstrlen(buf)==BLOCK_SIZE\twrite_to_file");
			return -1;
		}
		free(temp); free(free_block_num_str); free(fusedata_str); fclose(free_block_file);
		return 0;
	} else {
		// new_file_content will contain all previous blocks in the free block list plus the new number passed in
		char *new_file_content = (char *) calloc(1, BLOCK_SIZE);
		unsigned int some_free_block;
		while( buf != NULL ) {
			// turn the number into an int to test if it is just a 0
			some_free_block = atoi(buf);
			// if it's not a 0, add it the string that will be written back to the file
			if (some_free_block != 0) {
				strcat(new_file_content, buf);
				strcat(new_file_content, ",");
			}
			// get the next value in between commas
			buf = strtok(NULL, ",");
		}
		// put the new free_block_num along with a comma at the end of free block list in this file
		sprintf(temp, "%u,", free_block_num);
		strcat(new_file_content, temp);
		if ( write_to_file(new_file_content, free_block_file) != 0 ) {
			free(temp); free(buf); free(new_file_content); free(fusedata_str); fclose(free_block_file);
			printf("FAILURE:\tadd_free_block\telse statement\twrite_to_file");
			return -1;
		}
		free(temp); free(buf); free(new_file_content); free(fusedata_str); fclose(free_block_file);
		return 0;
	}
}

// removes the next free block from the file and returns 0 upon success
static int remove_next_free_block(unsigned int next_free_block, char *buf, FILE *fd)
{
	rewind(fd);
	// get the length of digits of the first free block number in the file
	unsigned int next_free_block_len;
	char *next_free_block_str = (char *) calloc(1, BLOCK_SIZE);
	sprintf(next_free_block_str, "%u", next_free_block);
	next_free_block_len = strlen(next_free_block_str);
	// append 0's to the contents of the file to replace the length of the free block number and the comma following it
	int i;
	for (i = 0; i <= next_free_block_len; ++i) {
		strcat(buf, "0");
	}
	// put the buffer without the removed free block back into the file
	if ( write_to_file(buf, fd) != 0 ) {
		logmsg("FAILURE:\tremove_next_free_block\twrite_to_file");
		free(next_free_block_str);
		return -1;
	}
	// the next free block was successfully removed and the free file list block was updated
	free(next_free_block_str);
	rewind(fd);
	return 0;
}

// returns an int that is the number of the next free block; if there are no free blocks, return -1
static int next_free_block()
{
	// set up variables
	int i;
	unsigned int next_free_block;
	char *fusedata_str = (char *) calloc(1, MAX_PATH_LENGTH);
	FILE *free_block_file;
	char *temp = (char *) calloc(1, BLOCK_SIZE);
	char *buf = (char *) calloc(1, BLOCK_SIZE);
	char *next_free_block_str;
	unsigned int temp_int;
	// loop through all free block list files if necessary
	for (i = FREE_START; i <= 2; ++i) {
		// append the number of the start of the free block list file and then open it
		sprintf(fusedata_str, "fusedata.%d", i);
		free_block_file = fopen(fusedata_str, "r+");
		// get the entire contents of the free block file
		fread(temp, BLOCK_SIZE, 1, free_block_file);
		rewind(free_block_file);
		// tokenize the file contents by commas
		next_free_block_str = strtok(temp, ",");
		// if there were no free blocks in the file, continue to the next file
		if (strlen(next_free_block_str) == BLOCK_SIZE) {
			fclose(free_block_file);
			continue;
		} else {
			// put the next_free_block in a variable that will be returned
			next_free_block = atoi(next_free_block_str);
			// get the next_free_block after the first one in this list
			next_free_block_str = strtok(NULL, ",");
			// keep going so long as that is not the end of the string
			while ( next_free_block_str != NULL ) {
				temp_int = atoi(next_free_block_str);
				// if the tokenized string was not a zero, add that to the buffer along with a comma
				if (temp_int != 0) {
					strcat(buf, next_free_block_str);
					strcat(buf, ",");
				}
				next_free_block_str = strtok(NULL, ",");
			}
			remove_next_free_block(next_free_block, buf, free_block_file);
			free(temp); free(buf); free(next_free_block_str); fclose(free_block_file);
			return next_free_block;
		}
	}
	free(temp); free(buf);
	// we got through all the free block files and there were no free blocks to return
	return -1;
}

// creates an inode block with a location that points to the file_block number; returns 0 on success
static int create_inode(unsigned int inode_block, unsigned int file_block)
{
	// open the fusedata block corresponding to this new inode_block number
	char inode_block_str[BLOCK_SIZE + 1];
	sprintf(inode_block_str, "fusedata.%u", inode_block);
	FILE *fd = fopen(inode_block_str, "r+");
	/* file inode format:
	 * {size:0, uid:1, gid:1, mode:33261, linkcount:1, atime:332442342, ctime:332442342, mtime:332442342,
	 * indirect:0, location:2444}
	 * size of the file is initially 0, there is only 1 link to the file at creation, there is no
	 * indirect initially because we don't know how large the file will be yet
	*/
	// get creation time of this filesystem in reference to the Epoch
	char creation_time_str[21]; // For 2014, time since Epoch would be 10 digits (so 20 possible digits is okay for now)
	sprintf(creation_time_str, "%lu", time(NULL)); // put the an unsigned int representing time directly into the creation_time_str string
	char buf[BLOCK_SIZE + 1];
	sprintf(buf, "{size:0, uid:%d, gid:%d, mode:33261, linkcount:1, atime:%s, ctime:%s, mtime:%s, indirect:0, location:%u}", UID, GID, creation_time_str, creation_time_str, creation_time_str, file_block);
	if ( write_to_file(buf, fd) != 0 ) {
		logmsg("FAILURE:\tcreate_inode\twrite_to_file");
		fclose(fd);
		return -1;
	}
	return 0;
}

// searches the path for a directory/file specified by type (0==dir, 1==file); returns the block number corresponding to the directory/file inode, or -1 if not path not valid
static int search_path(char *path, unsigned int type)
{
	int parts_to_path = count_chars(file_path, '/');
	// create an array of strings to contain all parts of the path
	char *path_parts[parts_to_path];
	char *temp_str;
	int i = 0;
	// tokenize the path and put the contents into a string array
	temp_str = strtok(file_path, "/");
	while (temp_str != NULL) {
		if (temp_str != "")
			path_parts[i++] = temp_str;
		temp_str = strtok(NULL, "/");
	}


/*
 * Return file attributes.
 * For the pathname, this should fill in the elements of "stat".
 * If a field is meaningless (e.g., st_ino) then it should be set to 0.
 * 
 * stat
 * ----
 * Return info about a file.
 * ino_t		st_ino			inode number		(ignored unless 'use_ino' mount option is given)
 * mode_t		st_mode			protection
 * nlink_t		st_nlink		number of hard links
 * off_t		st_size			total size, in bytes
 * clkcnt_t		st_blocks		number of __Byte blocks allocated
 * time_t		st_atime		time of last access
 * time_t		st_mtime		time of last modification
 * time_t		st_ctime		time of last status change
 * 
 * Following POSIX macros are defined to check the file type using the st_mode field:
 * S_ISREG - regular file?
 * S_ISDIR - directory?
 * S_ISBLK - block device?
*/
static int jb_getattr(const char *path, struct stat *stbuf)
{
/* previous code
	int res;
	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;
	return res;
*/
	
	int res;
	res = 0;
	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} /*else if (strcmp(path, KATZ_path) == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(KATZ_str);
	} else if (strcmp(path, "/testing") == 0){ 
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen("ht there")-3;
	} else if(strcmp(path, "/fusedata.0") == 0) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen("ht there")-3;
	}*/ else 
		res = -ENOENT;
// this puts the file attributes you want to show into the stat buffer
// so ls can output it to the screen from there
// this does not MAKE the file
	return res;
}

struct jb_dirp {
	/* DIR *
	 * directory stream
	 * Ordered sequence of all the directory entries in a particular directory.
	 * Directory entries represent files.
	 * Files may be removed or added from/to a directory asynchronously to the operation of readdir()
	*/
	DIR *dp;
	
	/* struct dirent *
	 * structure type used to return info about directory entries
	 * ----------------------------------------------------------
	 * char d_name[]			null-terminated file name component
	 * ino_t d_fileno			file serial number (for most file this is the same as the st_ino member that "stat" will return for a file)
	 * unsigned char d_name		length of the file name, not including term-null char
	 * unsigned char d_type		type of the file, possibly unknown
	*/
	struct dirent *entry;
	
	
	off_t offset;
};

/*
 * Open a directory for reading.
 * Unless the 'default_permissions' mount option is given, this method should check if opendir is permitted for this directory.
 * Optionally opendir may also return an arbitrary filehandle in the fuse_file_info struct, which will be passed to readdir, closedir, and fsyncdir
 * 
 * FUSE provides a "file handle" in the "fuse_file_info" structure.
 * The file handle is stored in the "fh" element of the structure, which is an unsigned 64-bit int (uint64_t) uninterpreted by FUSE.
 * If you choose to use it, you should set that field in your "open", "create", and "opendir" functions.
 * Other functions can then use it.
*/
static int jb_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	struct jb_dirp *d = malloc(sizeof(struct jb_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(path);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

// Returns (struct jb_dirp *) fi->fh which is a file handle
static inline struct jb_dirp *get_dirp(struct fuse_file_info *fi)
{
	// uintptr_t: int type capable of holding a value converted from a void pointer and then be converted back to that type with a value that compares equal to the original pointer
	return (struct jb_dirp *) (uintptr_t) fi->fh;
}

/* Read directory
 * Filesystem may choose between two modes of operation:
 * 	1)	readdir implementation ignores the offset parameter, and passes zero to the filler function's offset;
 * 		the filler function will not return '1' (unless an error happens), so the whole directory is read in a single readdir operation
 * 	2)	readdir implementation keeps track of the offsets of the directory entries;
 * 		it uses the offset parameter and always passes non-zero offset to the filler function;
 * 		when the buffer is full (or an error happens) the filler function will return '1'
 * 
 * Somehwat like "read", in that it starts at a given offset and returns results in a caller-supplied buffer.
 * The offset not a byte offset, and the results are a series of "struct dirents" rather than being uninterpreted bytes.
 * FUSE provides a "filler" function that will help put things into the buffer, making life easier.
 * 
 * General plan for a complete and correct "readdir":
 * --------------------------------------------------
 * 1)	Find the first directory entry following the given offset
 * 2)	Optionally, create a "struct stat" that describes the file as for getattr (but FUSE only looks at st_ino and the file-type bits of st_mode)
 * 3)	Call the "filler" function with arguments of buf, the null-terminated filename, the address of your "struct stat" (or NULL if you have none), and the offset of the next directory entry
 * 4)	If "filler" returns nonzero, or if there are no more files, return 0
 * 5)	Find the next file in the directory
 * 6)	Go back to step 2
 * 
 * From FUSE's POV, the offset is an uninterpreted off_t (i.e., unsigned int).
 * You provide an offset when you call "filler", and its possible that such an offset might come back to you as an argument later.
 * Typically, it's simply the byte offset (within your directory layout) of the directory entry, but it's really up to you.
 * 
 * NOTE: readdir can return errors in a number of instances; it can return -EBADF if the file handle is invalid, or -ENOENT if you use the path argument and the path doesn't exist
*/
static int jb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	/* fuse_fill_dir_t(void *buf, const char *name, const struct stat *stbuf, off_t off)
		buf:	the buffer passed to the readdir() operation
		name:	the file name of the directory entry
		stat:	file attributes, can be NULL
		off:	offset of the next entry or zero
	Function to add an entry in a readdir() operation
	*/
	
	/* previous code
	struct jb_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
	*/

	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
/*	filler(buf, KATZ_path + 1, NULL, 0);
	filler(buf, "/testing" + 1, NULL, 0);
	filler(buf, "/fusedata.0" +1, NULL, 0);*/
// the +1 is needed
// this just shows entries here for each file
// you're telling it the name of each file to show an entry for
// this does not CREATE any entries

	return 0;
	
}

/* Release directory
 * Similar to "release, except for directories.
*/
static int jb_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct jb_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

/* Create a directory with the given name
 * Note that the mode argument may not have the type specification bits set, (i.e., S_ISDIR(mode) can be false).
 * To obtain the dorrect directory type bits, use "mode|S_IFDIR".
 * 
 * Directory permissions are encoded in mode.
 * 
 * mkdir
 * -----
 * the argument "mode" specifies the permissions to use.
 * EDQUOT		user's quote of disk blocks or inodes on the filesystem has been exhausted
 * EEXIST		pathname already exists (not necessarily as a directory)
 * EFAULT		pathname points outside your accessible address space
 * EMLINK		number of links to the parent directory would exceed LINK_MAX
 * ENAMETOOLONG	pathname was too long
 * ENOENT		a directory component in pathname does not exist or is a dangling sym-link
 * ENOSPC		the device containing pathname has no room for the new directory
 * ENOTDIR		a component used as a directory in pathname is not, in fact, a directory
*/
static int jb_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

/* Remove a file
 * Remove the given file, sym-link, hard link, or special node.
 * NOTE: with hard links supported, unlink only deletes the data when the last hard link is removed.
 * 
 * unlink
 * ------
 * EBUSY		the file pathname cannot but unlinked because it is being used by the system or another process
 * EISDIR		pathname refers to a directory
 * ENAMETOOLONG	pathname was too long
 * ENOENT		a component in pathname does not exist or is a dangling symbolic link, or pathname is empty
 * ENOTDIR		a component used as a directory in pathname is not, in fact, a directory
*/
static int jb_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

/* Rename a file
 * Rename the file, directory, or other object "from" to the target "to".
 * Note that the source and target don't have to be in the same directory;
 * So it may be necessary to move the source to an entirely new directory
 * 
 * rename
 * ------
 * Change the name or location of a file.
 * Any other hard links to the file are unaffected.
 * Open file descriptors for oldpath are also unaffected.
 * If newpath already exists, it will be atomically replaced.
 * If oldpath and newpath are exists hard links reffering to the same file, then "rename" does nothing and returns a success status.
 * oldpath can specify a directory; in this case, newpath must either not exist, or it must specify an empty directory
 * ERRORS:
 * EBUSY		rename fails because oldpath/newpath is a directory that is in use by some process
 * EDQUOT		user's quote of disk blocks on the filesystem has been exhausted
 * EINVAL		the new pathname contained a path prefix of the old, or, more generally, an attempt was made to make a directory a subdir of itself
 * EISDIR		newpath is an existing directory, but oldpath is not a directory
 * EMLINK		oldpath already has the max number of links to it, or it was a directory and the directory containing newpath has the max number of links
 * ENAMETOOLONG	oldpath or newpath was too long
 * ENOENT		the link named by oldpath does not exist; or, a directory component in newpath does not exist; or oldpath/newpath is an empty str
 * ENOSPC		the device containing the file has no room for the new directory entry
 * ENOTDIR		a component used as a directory in oldpath or newpath is not, in fact, a directory; or, oldpath is a dir, and newpath exists but is not a dir
 * EEXIST		newpath is a nonempty dir, that is, contains entries other than '.' and '..'
*/
static int jb_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

/* Create a hard link between "from" and "to"
 * Make a new name for a file.
 * If newpath exists, it will NOT be overwritten.
 * 
 * link
 * ----
 * EDQUOT		the user's quote of disk blocks on the filesystem has been exhausted
 * EEXIST		newpath already exists
 * EMLINK		the file referred to by oldpath already has the max number of links to it
 * ENAMETOOLONG	oldpath/newpath was too long
 * ENOENT		a directory component in oldpath/newpath does not exist or is a dangling sym-link
 * ENOSPC		the device containing the file has no room for the new directory entry
 * ENOTDIR		a component used as a directory in oldpath/newpath is not, in fact, a directory
 * EPERM		oldpath is a directory
*/
static int jb_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

/* Create and open a file
 * If the file does not exist, first create it with the specified mode, and then open it
 * 
 * FUSE provides a "file handle" in the "fuse_file_info" structure.
 * The file handle is stored in the "fh" element of the structure, which is an unsigned 64-bit int (uint64_t) uninterpreted by FUSE.
 * If you choose to use it, you should set that field in your "open", "create", and "opendir" functions.
 * Other functions can then use it.
*/
static int jb_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;
	
	// get the 2 free blocks for the inode and actual file; if there isn't enough inodes, return an error
	int inode_block = next_free_block();
	if (inode_block == -1) {
		errno = EDQUOT; // all available free blocks are used up
		return -errno;
	}
	int file_block = next_free_block();
	if (file_block == -1) {
		errno = EDQUOT; // all available free blocks are used up
		add_free_block(inode_block); // inode_block was removed from the free block list, but since there isn't room to for the file block, the inode_block should be put back as free
		return -errno;
	}
	// create the inode data block and set it's location to point to the number of the file_block
	create_inode(inode_block, file_block);
	
	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

/* File open operation
 * Optionally "open" may also return an arbitrary filehandle in the "fuse_file_info" struct, which will be passed to all file opeartions
 * If file handles aren't used, this function should check for existence and permissions and return either success or an error code.
 * If file handles are used, you should also allocate any necessary structs and set fi->fh.
 * In addition, "fi" has some other fields that an advanced filesystem might find useful; see struct def in fuse_common.h
 * 
 * FUSE provides a "file handle" in the "fuse_file_info" structure.
 * The file handle is stored in the "fh" element of the structure, which is an unsigned 64-bit int (uint64_t) uninterpreted by FUSE.
 * If you choose to use it, you should set that field in your "open", "create", and "opendir" functions.
 * Other functions can then use it.
*/
static int jb_open(const char *path, struct fuse_file_info *fi)
{
	/* previous code
	int fd;

	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
	*/
	
// for every file here needs to check strcmp is okay
/*	if (strcmp(path, KATZ_path) != 0 && strcmp(path, "/testing") !=0 && strcmp(path, "/fusedata.0")  !=0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;
*/
	return 0;	
}

/* Read data from an open file
 * Read size bytes from the given file into the buffer buf, beginning offset bytes into the file.
 * Returns the number of bytes transferred, or 0 if offset was at or beyond the end of the file.
 * 
 * read
 * ----
 * Read from a file descriptor.
 * On success, the number of bytes read is returned (0 indicates EOF);
 * the file position is advanced by this number.
 * It is not an error if this number is smaller than the number of bytes requested;
 * this many happen for example because fewer bytes are actually available right now.
 * On error, -1 is returned, and errno is set appropriately.
 * EAGAIN		the file descriptor "fd" refers to a file other than a socket and has been marked nonoblocking (O_NONBLOCK), and the read would block
 * EBADF		fd is not a valid file descriptor or is not open for reading
 * EINTR		the call was interrupted by a signal before any data was read
 * EISDIR		fd refers to a directory
 * 
 * POSIX allows a read() that is interrupted after reading some data to return -1 (with errno set to EINTR) or to return the number of bytes already read
*/
static int jb_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	/* previous code
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
	*/
	
 	const char* KATZ_strz="ht there";
	size_t len;
	(void) fi;
	// for every file here needs to check strcmp is okay
	if(strcmp(path, KATZ_path) != 0 && strcmp(path, "/testing") !=0 && strcmp(path, "/fusedata.0")  !=0)
		return -ENOENT;

	if(strcmp(path, "/testing")==0){
		len = strlen(KATZ_strz);
	} else {
		len = strlen(KATZ_str);
	}
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
			if((strcmp(path, "/testing")!=0)) { KATZ_strz=KATZ_str; }
		memcpy(buf, KATZ_strz + offset, size);
	} else
		size = 0;

	return size;
}

/* Write data to an open file
 * Write should return exactly the number of bytes request except on error.
 * Similar to "read", except it CANNOT return 0 (0 indicates nothing was written to the file).
*/
static int jb_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

/* Get file system statistics
 * 'f_frsize', 'f_favail', 'f_fsid', and 'f_flag' fields are ignored
 * 
 * statvfs (description of the structure contents)
 * -----------------------------------------------
 * Returns info about a mounted filesystem.
 * path is the pathname of any file within the mounted filesystem;
 * buf is a pointer to a statvfs structure defined approximately as follows:
 * 	unsigned long	f_bsize		filesystem block size
 * 	fsblkcnt_t		f_blocks	size of fs in f_frsize (fragment size) units
 * 	fsblkcnt_t		f_bfree		free blocks
 * 	fsblkcnt_t		f_bavail	free blocks for unpriviledged users
 * 	fsfilcnt_t		f_files		inodes
 * 	fsfilcnt_t		f_ffree		free inodes
 * 	unsigned long	f_namemax	maximum filename length
 * 
 * <sys/types.h>
 * -------------
 * fsblkcnt_t		used for file system block counts
 * fsfilcnt_t		used for file system file counts
 * fsblkcnt_t, fsfilcnt_t, and ino_t	shall be defined as unsigned int types
*/
static int jb_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

/* Release an open file (called when there are no more references to an open file: all file descriptors are closed and all memory mappings are unmapped)
 * For every open() call there will be exactly one release() call with the same flags and file descriptor.
 * It is possible to have a file opened more than once, in which case only the last release will mean, that no more reads/writes will happen on the file.
 * 
 * No direct corresponding system call ("close" is related).
 * 
 * close
 * -----
 * Not checking the return value of "close" is a common but serious error.
 * Not checking the return value when closing the file may lead to silent loss of data.
 * However, "release" ignores the return value anyway.
*/
static int jb_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

/* Clean up filesystem (called on filesystem exit)
 * The "private_data" comes from the return value of init
*/
void jb_destroy(void *buf)
{

}

// Create the super-block (fusedata.0)
static int create_superblock(char *buf)
{
	FILE *superblock = fopen("fusedata.0", "w+");
	if (!superblock) {
		return -errno;
	}
	// fill superblock with 0's initially
	if ( write_to_file(buf, superblock) != 0 ) {
		logmsg("FAILURE:\tcreate_superblock\twrite_to_file\tbuf");
		return -errno;
	}
	rewind(superblock); // set position indicator of file stream to the beginning of the file
	
	// get creation time of this filesystem in reference to the Epoch
	char *creation_time_str = (char *) malloc(20); // For 2014, time since Epoch would be 10 digits (so 20 possible digits is okay for now)
	sprintf(creation_time_str, "%lu", time(NULL)); // put the an unsigned int representing time directly into the creation_time_str string
		
	/* format of super-block:
	 * {creationTime:1376483073,mounted:50,devId:20,freeStart:1,freeEnd:25,root:26,maxBlocks:10000}
	 * 
	 * createtime is created above, mounted increments with each mount (allocate a large set of bytes for this),
	 * devid is always 20, freestart is the first block containing the list of free blocks,
	 * freeend is the last block containing the list of free blocks, root is the block after freeend,
	 * maxblocks is arbitrary but was set to 10000 at first creation of this block OS
	*/	
	// mounted:1 because this is the first mount; freeStart, freeEnd, root, and maxBlocks will be defined constants for each new creation of a filesystem
	char *data = (char *) malloc(BLOCK_SIZE);
	sprintf(data, "{creationTime: %s, mounted: 1, devId: 20, freeStart: %u, freeEnd: %u, root: %u, maxBlocks: %u}", creation_time_str, FREE_START, FREE_END, ROOT, MAX_NUM_BLOCKS);
	
	if ( write_to_file(data, superblock) != 0 ) {
		logmsg("FAILURE:\tcreate_superblock\twrite_to_file\tdata");
		free(creation_time_str); free(data); fclose(superblock);
		return -errno;
	}

	fclose(superblock);

	free(creation_time_str); free(data);
	
	return 0;
}

// Set up comma separated values of free blocks after root
static int create_free_block_list(char *fusedata_digit, char *fusedata_str, unsigned int fusedata_digit_len)
{
	// the first block containing the free block list is a special case because it must not include the superblock, the blocks containing the free block list, or root
	sprintf(fusedata_digit, "%d", FREE_START); // fusedata_digit will hold the digit of the first block that holds the free block list
	strcpy(fusedata_str, "fusedata."); // initialize fusedata_str to contain "fusedata."
	strcat(fusedata_str, fusedata_digit); // fusedata_str will now contain "fusedata.i" where 'i' is a digit of a block that holds the free block list
	FILE *free_start_file = fopen(fusedata_str, "r+"); // create the fusedata.FREE_START file
	if (!free_start_file) {
		return -errno;
	}
	char *data = (char *) malloc(BLOCK_SIZE);
	// increment over 'i' up to BLOCKS_IN_FREE - 1 to write that free block to the free block list
	int i;
	for (i = ROOT + 1; i < BLOCKS_IN_FREE; ++i) {
		sprintf(data, "%u,", i);
		if ( write_to_file(data, free_start_file) != 0 ) {
			logmsg("FAILURE:\tcreate_free_block_list\twrite_to_file\tfree_start_file");
			free(data); fclose(free_start_file);
			return -errno;
		}
	}
	fclose(free_start_file);
	// every block after the starting one has BLOCKS_IN_FREE free blocks per block
	FILE *free_block_file;
	// increment over 'j' to create the free block list for each file from FREE_START + 1 up to FREE_END
	int j;
	for (j = FREE_START + 1; j <= FREE_END; ++j) {
		// get the fusedata.X file name where each increment of 'j' is 'X'
		sprintf(fusedata_digit, "%d", j);
		strcpy(fusedata_str, "fusedata.");
		strcat(fusedata_str, fusedata_digit);
		// open the fusedata.X file for reading and writing
		free_block_file = fopen(fusedata_str, "r+");
		if (!free_block_file) {
			return -errno;
		}
		// continue incrementing 'i' because 'i' left off where the next free block list should start
		for ( ; i < BLOCKS_IN_FREE * j; ++i) {
			sprintf(data, "%u,", i);
			if ( write_to_file(data, free_block_file) != 0 ) {
				logmsg("FAILURE:\tcreate_free_block_list\twrite_to_file\tfree_block_file");
				free(data); fclose(free_block_file);
				return -errno;
			}
		}
		fclose(free_block_file);
	}
	
	return 0;
}

// Set up the root directory's initial structure
static int create_root(char *fusedata_digit, char *fusedata_str)
{
	// open the root directory block file
	sprintf(fusedata_digit, "%d", ROOT);
	strcpy(fusedata_str, "fusedata.");
	strcat(fusedata_str, fusedata_digit);
	FILE *root_file = fopen(fusedata_str, "r+"); // open the fusedata.ROOT file
	if (!root_file) {
		return -errno;
	}
	char *data = (char *) malloc(BLOCK_SIZE);
	char *creation_time_str = (char *) malloc(20); // For 2014, time since Epoch would be 10 digits (so 20 possible digits is okay for now)
	sprintf(creation_time_str, "%lu", time(NULL)); // put the an unsigned int representing time directly into the creation_time_str string
	/* format of root-block:
	 * {size:0, uid:1, gid:1, mode:16877, atime:1323630836, ctime:1323630836, mtime:1323630836, linkcount:2, filename_to_inode_dict: {d:.:26,d:..:26}}
	 * mode:16877 converts to 40755 in octal; the 40000 refers to the fact that this is a directory
	 * 
	 * size is initially 0, uid, gid, & mode will not change for this filesystem;
	 * atime (access time), ctime (inode or file change time), mtime (file modification time) are all the same when root is initialized;
	 * linkcount is 2 because '.' and '..' point to root, and filename_to_inode_dict denotes the links to inodes root has stored
	*/
	sprintf(data, "{size:0, uid:%d, gid:%d, mode:16877, atime:%s, ctime:%s, mtime:%s, linkcount:2, filename_to_inode_dict: {d:.:%d,d:..:%d}}", UID, GID, creation_time_str, creation_time_str, creation_time_str, ROOT, ROOT);
	if ( write_to_file(data, root_file) != 0 ) {
		logmsg("FAILURE:\tcreate_root\twrite_to_file");
		free(data); free(creation_time_str); fclose(root_file);
		return -errno;
	}
	
	fclose(root_file);
	
	return 0;
}

// Creates fusedata.1 to fusedata.X blocks (X is MAX_NUM_BLOCKS-1)
static int create_blocks(char *buf, char *fusedata_digit, char *fusedata_str, unsigned int fusedata_digit_len)
{	
	int i;
	for (i = 1; i < MAX_NUM_BLOCKS; ++i) {
		sprintf(fusedata_digit, "%d", i); // fusedata_digit will hold from 0 --> MAX_NUM_BLOCKS-1
		strcpy(fusedata_str, "fusedata."); // initialize fusedata_str to contain "fusedata." on every loop to concat the block number onto it
		strcat(fusedata_str, fusedata_digit); // fusedata_str will now contain "fusedata.i" where 'i' goes up to MAX_NUM_BLOCKS-1
		FILE *fd = fopen(fusedata_str, "w+"); // create the fusedata.X file
		if (!fd) {
			return -errno;
		}
		if ( write_to_file(buf, fd) != 0 ) {
			fclose(fd);
			return -errno;
		}
		fclose(fd);
	}

	// create the free block list
	if ( create_free_block_list(fusedata_digit, fusedata_str, fusedata_digit_len) != 0 ) {
		logmsg("FAILURE:\tcreate_blocks\tcreate_free_blocklist");
		return -errno;
	}
	// create root
	if ( create_root(fusedata_digit, fusedata_str) != 0 ) {
		logmsg("FAILURE:\tcreate_blocks\tcreate_root");
		return -errno;
	}

	return 0;
}

// Essentially just updates the number of times mounted and writes that back to the fusedata.0 file
static int update_superblock()
{
	char *temp = (char *) malloc(BLOCK_SIZE); // used to store string values that are already known and thus just need to pass over in the file stream
	FILE *superblock = fopen("fusedata.0", "r+"); // open the super-block with read + write permissions (but don't overwrite the file)
	if (!superblock) {
		return -errno;
	}
	char *creation_time_str = (char *) malloc(20); // For 2014, time since Epoch would be 10 digits (so 20 possible digits is okay for now)
	unsigned int mount_num;
	// we know certain contents in the file such as "{creationTime:", "mounted:", "devId:", "20,", "freeStart:", "1,", etc... so just put those in temp
	// we don't know the creation_time which is the second specifier, and we need the number of times mounted to increment it and write back to the file which is the fifth specifier
	fscanf(superblock, "%s %s %s %u %s %s %s %s %s %s %s %s %s %s %s", temp, creation_time_str, temp, &mount_num, temp, temp, temp, temp, temp, temp, temp, temp, temp, temp, temp);
	mount_num++; // increment the number of times mounted since the filesystem is being mounted again
	rewind(superblock); // point the file stream back to the beginning to overwrite the data with the new mount number
	// the %s for the actual created time was scanned as a string so the ',' char following it is also put into creation_time_str
	sprintf(temp, "{creationTime: %s mounted: %u devId: 20, freeStart: %u, freeEnd: %u, root: %u, maxBlocks: %u}", creation_time_str, mount_num, FREE_START, FREE_END, ROOT, MAX_NUM_BLOCKS);
	if ( write_to_file(temp, superblock) != 0) {
		logmsg("FAILURE:\tupdate_superblock\twrite_to_file");
		free(temp); free(creation_time_str); fclose(superblock);
		return -errno;
	}
	//fprintf(superblock, "{creationTime: %s mounted: %u devId: 20, freeStart: %u, freeEnd: %u, root: %u, maxBlocks: %u}", creation_time_str, mount_num, FREE_START, FREE_END, ROOT, MAX_NUM_BLOCKS);
	
	fclose(superblock);
	
	free(temp); free(creation_time_str);
	
	return 0;
}

/* Initialize filesystem
 * The return value will be passed in the "private_data" field of "fuse_context" to all file operations and as a parameter to the "destroy" method
 * 
 * fuse_conn_info: Gives info about what features are supported by FUSE, and can be used to request certain capabilities.
 * The return value of this function is available to all file operations in the "private_data" field of "fuse_context".
 * It is also passed as a paramater to the destroy() method.
*/
void * jb_init(struct fuse_conn_info *conn)
{
	/* Undocumented but extraordinarly useful fact:
		fuse_context is set up before this function is called;
		fuse_get_context()->private_data returns the user_data passed to fuse_main().
		Really seems like either it should be a third parameter coming in here, or else the fact should be documented.
	*/
	chdir(fuse_get_context()->private_data); // FILES_DIR is the directory where the fusedata.X files will be located
	if (!file_exists("fusedata.0")) {
		// setting a string with BLOCK_SIZE number of 0's to set up fusedata.X files with
		int char_zero = '0';
		char *buf = (char *) malloc(BLOCK_SIZE); // 4096 bytes to a file initially
		memset(buf, char_zero, BLOCK_SIZE); // initialize buf to BLOCK_SIZE 0's
		// setting up strings for fusedata.X to make strings for 'X' values
		char *temp_str = (char *) malloc(MAX_BLOCK_DIGITS); // this will represent the 'X' in fusedata.X files
		sprintf(temp_str, "%d", MAX_NUM_BLOCKS-1); // MAX_NUM_BLOCKS-1 because files go from fusedata.0 to fusedata.(MAX_NUM_BLOCKS-1)
		unsigned int fusedata_digit_len = strlen(temp_str); // largest possible char length to represent MAX_NUM_BLOCKS as a string
		char *fusedata_digit = (char *) malloc(fusedata_digit_len); // this will represent the 'X' in fusedata.X files
		char *fusedata_str = malloc(MAX_PATH_LENGTH); // this will initially contain "fusedata."
		// create super-block (fusedata.0)
		if ( create_superblock(buf) != 0) {
			logmsg("FAILURE:\tjb_init\tcreate_superblock");
			// ERROR --> exit
			exit(1);
		}
		// create fusedata.X blocks
		if ( create_blocks(buf, fusedata_digit, fusedata_str, fusedata_digit_len) != 0 ) {
			logmsg("FAILURE:\tjb_init\tcreate_blocks");
			// ERROR --> exit
			exit(1);
		}
		
		free(buf); free(temp_str); free(fusedata_digit); free(fusedata_str);		
	} else { // fusedata.0 already exists (update the mounted variable in fusedata.0)
		if ( update_superblock() != 0 ) {
			logmsg("FAILURE:\tjb_init\tupdate_superblock");
			// ERROR --> exit
			exit(1);
		}
	}
	return fuse_get_context()->private_data;
}


static struct fuse_operations jb_oper = {
	// Functions needed for Filesystem (Part 1)
	.getattr	= jb_getattr,
	//.create		= jb_create,
	.open		= jb_open,
	.read		= jb_read,
	//.write		= jb_write,
	//.statfs		= jb_statfs,
	//.release	= jb_release,
	//.destroy	= jb_destroy,
	.init		= jb_init,
	//.link		= jb_link,
	//.mkdir		= jb_mkdir,
	//.opendir	= jb_opendir,
	.readdir	= jb_readdir,
	//.releasedir	= jb_releasedir,
	//.rename		= jb_rename,
	//.unlink		= jb_unlink,
	//// Functions needed for Filesystem (Part 1)
	.flag_nullpath_ok = 0,
	/* .flag_nullpath_ok
	 * ------------------
	 * Set so that code can accept a NULL path argument (because it can get file info from fi->fh) for
	 * the following operations: flush, lock, read, readdir, release, releasedir, write, etc.
	*/
};

int main(int argc, char *argv[])
{
	char *pathname = realpath(FILES_DIR, NULL);
	return fuse_main(argc, argv, &jb_oper, pathname);
}
