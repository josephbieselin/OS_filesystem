#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <stdlib.h>
#include <malloc.h>

static const char *KATZ_str ="hi";
static const char *KATZ_path = "/KATZz";


/*

fusermount -u ~/Desktop/sandboxs/mount; gcc -Wall -g ~/Desktop/sandboxs/os.c `pkg-config fuse --cflags --libs` -o OS_hw; ./OS_hw ./mount; ls -al mount

*/
static int KATZ_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else if (strcmp(path, KATZ_path) == 0) {
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
	} else 
		res = -ENOENT;
// this puts the file attributes you wan to show into the stat buffer
// so ls can output it to the screen from there
// this does not MAKE the file
	return res;
}

static int KATZ_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (strcmp(path, "/") != 0)
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, KATZ_path + 1, NULL, 0);
	filler(buf, "/testing" + 1, NULL, 0);
	filler(buf, "/fusedata.0" +1, NULL, 0);
// the +1 is needed
// this just shows entries here for each file
// you're telling it the name of each file to show an entry for
// this does not CREATE any entries

	return 0;
}

static int KATZ_open(const char *path, struct fuse_file_info *fi)
{
// for every file here needs to check strcmp is okay
	if (strcmp(path, KATZ_path) != 0 && strcmp(path, "/testing") !=0 && strcmp(path, "/fusedata.0")  !=0)
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	return 0;
}

static int KATZ_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
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





const int FILE_SIZE=4096;
const int MAX_FILES=10000;
struct reg_file{
	int used_size;
	char* data;
};
// Undocumented but extraordinarily useful fact:  the fuse_context is
// set up before this function is called, and
// fuse_get_context()->private_data returns the user_data passed to
// fuse_main().  Really seems like either it should be a third
// parameter coming in here, or else the fact should be documented
// (and this might as well return void, as it did in older versions of
// FUSE).
static void* KATZ_init(struct fuse_conn_info *conn)
{
	int letter='0';
 	char* buf;
 	buf=(char *)malloc(FILE_SIZE); // 4096 bytes to a file
 	memset(buf, letter, FILE_SIZE);
	
	int file_nums= MAX_FILES;
	file_nums--; // 0 indexing means last number is 1 less than MAX
	char* num_digits;
	num_digits=malloc(100); // we're not supporting more than 100 digits
	sprintf(num_digits,"%d",file_nums);
	
	char* letter_ch;
	letter_ch=(char *)malloc(strlen(num_digits)); //the # of digits in the string
	
	char *name;
	name=(char *) malloc(255*4*4); // arbitrary maximum size
	// in theory its the strlen of the private_data + strlen num_digits + 1
	// but strlen private_data is unknown
	// so this will fail for mount directory of length >255*16 bytes
	int i;
	for(i=0; i<1; i++){
		snprintf(letter_ch, 5, "%d", i); 
		strcpy(name, fuse_get_context()->private_data);
		strcat(name, "fusedata.");
		strcat(name, letter_ch);
		FILE* fd=fopen(name,"w+");
		fwrite(buf, FILE_SIZE, 1, fd);
		fclose(fd);
	}

	free(letter_ch);
	free(name);
	free(num_digits);
 	return NULL;
}

// if have only init, cannot read the mount dir, no readdir function implemented
static struct fuse_operations KATZ_oper = {
	.init = KATZ_init,
	.getattr	= KATZ_getattr,
	.readdir	= KATZ_readdir,
	.open		= KATZ_open,
	.read		= KATZ_read,
};

int main(int argc, char *argv[])
{
	int x= MAX_FILES;
	x--;
	char* ch;
	ch=malloc(100);
	sprintf(ch,"%d",x);
	char* fullpath = realpath(argv[argc-1], NULL);
	return fuse_main(argc, argv, &KATZ_oper, fullpath);
}

	
