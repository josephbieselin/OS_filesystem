/*
	Joseph Bieselin
	N 16590510
	jab975

	HW 2:
	Filesystem
*/

#include <stdlib.h>	// utility functions: atoi, atol
#include <stdio.h>	// printf, fprintf, fgets, fputs
#include <string.h>	// strcpy, strcmp, strncmp, strtok, strlen
#include <assert.h>	// debugging: assert


/*
DEFINED CONSTANTS TO BE USED
#define ____ ###
*/

/*
Expected arguments (in specified order): jabFS [FUSE and mount options] rootDir mountPoint
*/
int main(int argc, char *argv[])
{
	int fuse_stat;
	struct jab_state *jab_data; // 
	// Check that at least 2 arguments were passed: root and mount point
	if(argc < 3)
	{
		// not enough arguments so print to standard error and abort
		fprintf(stderr, "expected command: jabFS [FUSE and mount options rootDir mountPoint\n");
		abort();
	}
}
