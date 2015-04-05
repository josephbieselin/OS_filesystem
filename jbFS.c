/*
  FUSE: Filesystem in Userspace

  Operating Systems - CS 3224
  HW 2 - Filesystem
  * Initialized from fusemp_fh.c
  * Programmer: Joseph Bieselin
	
  gcc -Wall jbFS.c `pkg-config fuse --cflags --libs` -lulockmgr -o jbFS
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE

#include <fuse.h>
#include <ulockmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <sys/file.h> /* flock(2) */


// Filesystem Constants (these can be changed later)
#define MAX_NUM_BLOCKS 	10000	// number of blocks in the filesystem
#define MAX_FILE_SIZE 	1638400	// max size in bytes of a file
#define BLOCK_SIZE 		4096	// size in bytes of a block

// Other Constants
#define FILES_DIR "/fusedata"	// directory to put fusedata.X files

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
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

/*
 * If path is a symbolic link, fill buf with its target, up to size.
 * Not required if you don't support symbolic links.
 * 
 * Symbolic-link support requires only readlink and symlink.
 * FUSE will take care of tracking symbolic links in paths, so path-eval code doesn't need to worry about it.
*/
static int jb_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
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
 * 		it uses the offset parameter and always passes non'zero offset to the filler function;
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

	fd = open(path, fi->flags, mode);
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
	int fd;

	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
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
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
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
 * For every open() call there will be exactly one release() call with the sanem flags and file descriptor.
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

/* Initialize filesystem
 * The return value will be passed in the "private_data" field of "fuse_context" to all file operations and as a parameter to the "destroy" method
 * 
 * fuse_conn_info: Gives info about what features are supported by FUSE, and can be used to request certain capabilities.
 * The return value of this function is available to all file operations in the "private_data" field of "fuse_context".
 * It is also passed as a paramater to the destroy() method.
*/
void * jb_init(struct fuse_conn_info *conn)
{
		
		return NULL;
}

/* rmdir() and symlink() are unused in HW 2
// NOT A NECESSARY FUNCTION TO CREATE FOR PROJECT ?????????????????????
static int jb_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

// NOT A NECESSARY FUNCTION TO CREATE FOR PROJECT ?????????????????????
static int jb_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}
*/

static struct fuse_operations jb_oper = {
	// Functions needed for Filesystem (Part 1)
	.getattr	= jb_getattr,
	.create		= jb_create,
	.open		= jb_open,
	.read		= jb_read,
	.write		= jb_write,
	.statfs		= jb_statfs,
	.release	= jb_release,
	.destroy	= jb_destroy,
	.init		= jb_init,
	.link		= jb_link,
	.mkdir		= jb_mkdir,
	.opendir	= jb_opendir,
	.readdir	= jb_readdir,
	.readlink	= jb_readlink,
	.releasedir	= jb_releasedir,
	.rename		= jb_rename,
	.unlink		= jb_unlink,
	// Functions needed for Filesystem (Part 1)
	.flag_nullpath_ok = 0,
	/* .flag_nullpath_ok
	 * ------------------
	 * Set so that code can accept a NULL path argument (because it can get file info from fi->fh) for
	 * the following operations: flush, lock, read, readdir, release, releasedir, write, etc.
	*/
};

int main(int argc, char *argv[])
{
	// chdir(FILES_DIR); char* files_path = (char *) malloc(MAX_PATH); getcwd(files_path, MAX_PATH);
	return fuse_main(argc, argv, &jb_oper, NULL); // files_path);
}
