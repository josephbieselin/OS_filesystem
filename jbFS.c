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

static int jb_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_chmod(const char *path, mode_t mode)
{
	int res;

	res = chmod(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_truncate(const char *path, off_t size)
{
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int jb_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int jb_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int jb_open(const char *path, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

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

static int jb_read_buf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;

	src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

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

static int jb_write_buf(const char *path, struct fuse_bufvec *buf,
		     off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

	(void) path;

	dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	dst.buf[0].fd = fi->fh;
	dst.buf[0].pos = offset;

	return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

static int jb_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int jb_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

static int jb_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int jb_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int jb_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int jb_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);
	if (res == -1)
		return -errno;
	return res;
}

static int jb_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);
	if (res == -1)
		return -errno;
	return res;
}

static int jb_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

static int jb_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}

static int jb_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void) path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}

static struct fuse_operations jb_oper = {
	// Functions needed for Filesystem (Part 1)
	.getattr	= jb_getattr,
	.create		= jb_create,
	.open		= jb_open,
	.read		= jb_read,
	.write		= jb_write,
	.statfs		= jb_statfs,
	.release	= jb_release,
	.destroy	= jb_destory;
	.init		= jb_init;
	.link		= jb_link;
	.mkdir		= jb_mkdir;
	.opendir	= jb_opendir;
	.readdir	= jb_readdir;
	.readlink	= jb_readlink;
	.releasedir	= jb_releasedir;
	.rename		= jb_rename;
	.unlink		= jb_unlink;
	// Functions needed for Filesystem (Part 1)
};

int main(int argc, char *argv[])
{
	chdir(FILES_DIR);
	return fuse_main(argc, argv, &jb_oper, files_path);
}
