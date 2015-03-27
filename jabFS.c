/*
	Joseph Bieselin
	N 16590510
	jab975

	HW 2:
	Filesystem
*/

#include <fuse.h>
#include <stdlib.h>	// utility functions: atoi, atol
#include <stdio.h>	// printf, fprintf, fgets, fputs
#include <string.h>	// strcpy, strcmp, strncmp, strtok, strlen
#include <assert.h>	// debugging: assert

// DEFINED CONSTANTS TO BE USED (these values may be changed in the future)
#define MAX_NUM_BLOCKS 10000
#define MAX_FILE_SIZE 1638400
#define BLOCK_SIZE 4096

// Default path files
static const char *files_path = "/fuse";

// fuse_conn_info contains connection info that is passed to the ->init() method
// initialize fusedata blocks from fusedata.0 to fusedata.(MAX_NUM_BLOCKS-1)
// Each block is preallocated (upon FS creation) with all zeros and stored on the host file system
void *jab_init(struct fuse_conn_info *conn)
{

}


// redefined functions that will call FUSE functions to implement UNIX commands
/*
At a minimum, you must support the following functions:
create
destroy
getattr
init
link
mkdir
open
opendir
read
readdir
readlink
release
releasedir
rename
statfs
unlink
write
*/
static struct fuse_operations jab_oper = {
	.init = jab_init
};


/*
Expected arguments (in specified order): jabFS [FUSE and mount options] rootDir mountPoint
*/
int main(int argc, char *argv[])
{
	int fuse_stat;
	struct jab_state *jab_data; // useful due to fuse_context -> private_date always being available as private filesystem data
	// Check that at least 2 arguments were passed: root and mount point
	if(argc < 3)
	{
		// not enough arguments so print to standard error and abort
		// Code below is based on error checking from following URL: http://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/
		fprintf(stderr, "expected command: jabFS [FUSE and mount options rootDir mountPoint\n");
		abort();
	}
	
	jab_data = malloc(sizeof(struct jab_state));
	if(jab_data == NULL)
	{
		fprintf(stderr, "ERROR: fuse_context for private filesystem data could not be allocated any memory\n");
		abort();
	}
	
	// set the directory where the filesystem will be located
	// assume the second to last argument passed is the root directory and have the function return that value
	jab_data->rootdir = realpath(argv[argc-2], NULL);
	
	// return 0 on a successful call
	// jabFS_oper will define the functions created for this filesystem
	// jab_data is the user data that will be supplied in the context during init()
	return fuse_main(argc, argv, &jabFS_oper, jab_data);
}
