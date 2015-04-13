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
#define MAX_FILENAME_LEN	3800// based on how large a filename could fit in root dir if it were the only entry
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
	for (i = FREE_START; i <= FREE_END; ++i) {
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
	 *  indirect:0, location:2444}
	 * size of the file is initially 0, there is only 1 link to the file at creation, there is no
	 * indirect initially because we don't know how large the file will be yet
	*/
	// get creation time of this inode in reference to the Epoch
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

// creates a directory inode block with to 2 links in inode_dict ('.' to self, '..' to parent); returns 0 on success
static int create_dir(unsigned int dir_block, unsigned int parent_block)
{
	// open the fusedata block corresponding to this new dir_block number
	char dir_block_str[BLOCK_SIZE + 1];
	sprintf(dir_block_str, "fusedata.%u", dir_block);
	FILE *fd = fopen(dir_block_str, "r+");
	/* directory format:
	 * {size:BLOCK_SIZE, uid:1000, gid:1000, mode:16877, atime:332442342, ctime:332442342, mtime:332442342, linkcount:2,
	 *  filename_to_inode_dict: {d:.:dir_block,d:..:parent_block}}
	 * size of the block is the block size, there is only 2 links initially (this dir and the parent dir)
	*/
	// get creation time of this directory in reference to the Epoch
	char creation_time_str[21]; // For 2014, time since Epoch would be 10 digits (so 20 possible digits is okay for now)
	sprintf(creation_time_str, "%lu", time(NULL)); // put the an unsigned int representing time directly into the creation_time_str string
	char buf[BLOCK_SIZE + 1];
	sprintf(buf, "{size:%d, uid:%d, gid:%d, mode:16877, atime:%s, ctime:%s, mtime:%s, linkcount:2, filename_to_inode_dict: {d:.:%u,d:..:%u}}", BLOCK_SIZE, DIR_UID, DIR_GID, creation_time_str, creation_time_str, creation_time_str, dir_block, parent_block);
	if ( write_to_file(buf, fd) != 0 ) {
		logmsg("FAILURE:\tcreate_inode\twrite_to_file");
		fclose(fd);
		return -1;
	}
	return 0;
	
}


// type (0==dir, 1==file); when atime, ctime, or mtime is 1, the passed in FILE stream's respective time fields get updated; returns a 1 if the new file size will be too large
static int update_time(unsigned int type, unsigned int atime, unsigned int ctime, unsigned int mtime, FILE *fd)
{
	// get the current time
	char new_time[BLOCK_SIZE];
	sprintf(new_time, "%lu,", time(NULL));
	// variables to be used in fscanf function of the file stream
	char buf[BLOCK_SIZE + 1];
	char size[BLOCK_SIZE]; char uid[BLOCK_SIZE]; char gid[BLOCK_SIZE]; char mode[BLOCK_SIZE]; char linkcount[BLOCK_SIZE];
	char atime_str[BLOCK_SIZE]; int old_atime;
	char ctime_str[BLOCK_SIZE]; int old_ctime;
	char mtime_str[BLOCK_SIZE]; int old_mtime;
	char fname_inode[BLOCK_SIZE]; char inodes[BLOCK_SIZE];
	char indirect[BLOCK_SIZE]; char location[BLOCK_SIZE];
	char temp[10];
	// format will be for a directory
	if (type == 0) {
		// for whichever time value is equal to 1, update that time field with the new_time set at the beginning of the function
		if (atime == 1) {
			fscanf(fd, "%s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s %s", size, uid, gid, mode, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, linkcount, fname_inode, inodes);
			if (sprintf(buf, "%s %s %s %s atime:%s ctime:%i, mtime:%i, %s %s %s", size, uid, gid, mode, new_time, old_ctime, old_mtime, linkcount, fname_inode, inodes) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		if (ctime == 1) {
			fscanf(fd, "%s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s %s", size, uid, gid, mode, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, linkcount, fname_inode, inodes);
			if (sprintf(buf, "%s %s %s %s atime:%i, ctime:%s mtime:%i, %s %s %s", size, uid, gid, mode, old_atime, new_time, old_mtime, linkcount, fname_inode, inodes) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		if (mtime == 1) {
			fscanf(fd, "%s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s %s", size, uid, gid, mode, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, linkcount, fname_inode, inodes);
			if (sprintf(buf, "%s %s %s %s atime:%i, ctime:%i, mtime:%s %s %s %s", size, uid, gid, mode, old_atime, old_ctime, new_time, linkcount, fname_inode, inodes) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		return 0;
	} else { // format will be for a file
		// for whichever time value is equal to 1, update that time field with the new_time set at the beginning of the function
		if (atime == 1) {
			fscanf(fd, "%s %s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s", size, uid, gid, mode, linkcount, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, indirect, location);
			if (sprintf(buf, "%s %s %s %s %s atime:%s ctime:%i, mtime:%i, %s %s", size, uid, gid, mode, linkcount, new_time, old_ctime, old_mtime, indirect, location) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		if (ctime == 1) {
			fscanf(fd, "%s %s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s", size, uid, gid, mode, linkcount, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, indirect, location);
			if (sprintf(buf, "%s %s %s %s %s atime:%i, ctime:%s mtime:%i, %s %s", size, uid, gid, mode, linkcount, old_atime, new_time, old_mtime, indirect, location) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		if (mtime == 1) {
			fscanf(fd, "%s %s %s %s %s %6c%i%*c %6c%i%*c %6c%i%*c %s %s", size, uid, gid, mode, linkcount, atime_str, &old_atime, ctime_str, &old_ctime, mtime_str, &old_mtime, indirect, location);
			if (sprintf(buf, "%s %s %s %s %s atime:%i, ctime:%i, mtime:%s %s %s", size, uid, gid, mode, linkcount, old_atime, old_ctime, new_time, indirect, location) > BLOCK_SIZE) {
				return 1;
			}
			rewind(fd);
			if ( write_to_file(buf, fd) != 0 ) {
				return -1;
			}
			fread(temp, 1, 1, fd); rewind(fd);
		}
		return 0;
	}
}

// returns the block number if name and type match the entry; name = file/dir name to search for, type (0==dir, 1==file), entry = "type:name:block_number"
static int compare_dir_entry(char *name, unsigned int type, char *entry)
{
	// entry_vals shall contain three indexes: 0 = type ('f' or 'd'), 1 = name, 2 = block_number
	char *entry_vals[3];
	int i = 0;
	entry_vals[i] = strtok(entry, ":");
	while (entry_vals[i++] != NULL) {
		entry_vals[i] = strtok(NULL, ":");
	}
	// if type we are looking for matches the type of this entry, check the name
	if ( ( (type == 0) && (strcmp("d", entry_vals[0]) == 0) )  ||  ( (type == 1) && (strcmp("f", entry_vals[0]) == 0) ) ) {
		// if the passed in name matches the entry's name, return the entry's block_number
		if ( strcmp(name, entry_vals[1]) == 0 ) {
			int block_num = atoi(entry_vals[2]);
			//return entry_vals[2];
			return block_num;
		}
	}
	// either the types did not match or the names did not match, so return -1
	return -1;
	
}

// searches for name inside of the fusedata.X block corresponding to dir_block; type (0==dir, 1==file); return the block number if name is found and the types match
static int search_dir(char *name, unsigned int type, unsigned int dir_block)
{
	// open the file corresponding to fusedata.dir_block
	char block_str[BLOCK_SIZE];
	sprintf(block_str, "fusedata.%u", dir_block);
	FILE *fd = fopen(block_str, "r+");
	// update the access time of the directory since we are searching through it
	int temp = update_time(0, 1, 0, 0, fd);
	if (temp == 1) {
		logmsg("FAILURE:\tsearch_dir\tupdate_time\tno more space");
		return temp;
	} else if (temp == -1) {
		logmsg("FAILURE:\tsearch_dir\tupdate_time\tfailed to write");
		return temp;
	}
	// if update_time did not return a -1 or 1, it was successful
	// read the file block into a char array that will be parsed
	char file_contents[BLOCK_SIZE + 1];
	fread(file_contents, BLOCK_SIZE, 1, fd);
	// tokenize the directory contents by the brackets
	int i = 0;
	char *dir_entries[3]; // 3 because there is 1) dir info, 2) dir entries (which is what is needed), 3) 0's after the closing '}' chars
	dir_entries[i] = strtok(file_contents, "{}");
	while (dir_entries[i++] != NULL) {
		dir_entries[i] = strtok(NULL, "{}");
	}
	// dir_entries[1] contains the directory entries; the number of entires is the number of commas + 1
	int num_entries = count_chars(dir_entries[1], ',');
	++num_entries;
	char *entry_names[num_entries];
	// tokenize the entries by commas to get each type, name, and block number
	i = 0;
	entry_names[i] = strtok(dir_entries[1], ","); // each index will contain: "type:name:number" --> type = 'f' or 'd' for file or dir, name = the name of the entry to compare too, number = block number of the entry
	while (entry_names[i++] != NULL) {
		entry_names[i] = strtok(NULL, ",");
	}
	int temp_block = -1;
	for (i = 0; i < num_entries; ++i) {
		// compare a directory entry and return the block number of the entry if it is found
		temp_block = compare_dir_entry(name, type, entry_names[i]);
		if (temp_block > 0) {
			break; // we found a match for our file/directory, so break
		}
	}
	// if the directory/file name was not found, temp will equal -1; otherwise, it will equal the block number of the found entry
	fclose(fd);
	return temp_block;
}

// searches the path for a directory/file specified by type (0==dir, 1==file); returns the block number corresponding to the directory/file inode, 0 if the dir/file does not exist, or -1 if the path was not valid
static int search_path(const char *the_path, unsigned int type)
{
	// if one backslash is passed, return the root's block number
	if ( (strcmp(the_path, "/") == 0) && (type == 0) ) {
		return ROOT;
	}
	char path[BLOCK_SIZE + 1];
	strcpy(path, the_path);
	// parts_to_path will be 1 if the search path is similar to "/dir"; it will be 2 if the search path is similar to "/other/dir"
	int parts_to_path = count_chars(path, '/');
	// create an array of strings to contain all parts of the path
	char *path_parts[parts_to_path - 1];
	//char *temp_str;
	int i = 0;
	// tokenize the path and put the contents into a string array
	path_parts[i] = strtok(path, "/");
	//temp_str = strtok(path, "/");
	while (path_parts[i++] != NULL) {
		path_parts[i] = strtok(NULL, "/");
	}
	i = 0;
	int block_num;
	// if there is more than one '/' in the path, the first part of the name inside the root dir will be a directory
	if ( parts_to_path > 1 ) {
		block_num = search_dir(path_parts[i], 0, ROOT);
	} else { // we are looking for something in root, so find it based whatever the passed in type was
		block_num = search_dir(path_parts[i], type, ROOT);
	}
	// if we are looking at root dir and the file/dir was not found, return 0
	if ( (parts_to_path == 1) && (block_num == -1) ) {
		return 0;
	} else if ( (parts_to_path == 1) && (block_num > 0) ) { // there was only 1 element to search for in root and it was found
		return block_num;
	}
	// we are looking in sub-directories of root, so until we get to the last element of the path, we are looking for directory names
	if ( block_num == -1 ) {
		return -1; // return -1 because the first directory in path inside of root was not found
	}
	// if the last element of path_parts is NULL (possibly because the passed in path had an ending "/" for no reason, i.e. "/file/path/"), decrement parts_to_path so the last element isn't searched
	if ( path_parts[parts_to_path - 1] == NULL)
		--parts_to_path;
	// search for "/" delimited element from the passed in path, returning the block number of the element if found; return -1 if an illegitimate directory is passed in the path, or 0 if all elements were legitimate except for the last element
	for (i = 1; i < parts_to_path; ++i) {
		// if the last element of the path is the next part to be searched
		if ( i == (parts_to_path - 1) ) {
			block_num = search_dir(path_parts[i], type, block_num);
			// if file/dir was not found, it does not exist so return 0
			if (block_num == -1) {
				return 0;
			} else { // the final element was found so return the block number
				return block_num;
			}
		} else { // we are not at the last element of the path, so we are searching for directory names
			block_num = search_dir(path_parts[i], 0, block_num);
			// the directory name was not found, so the path is invalid; return a -1
			if (block_num == -1) {
				return -1;
			} else { // the directory name was found, but we have not reached the end of path, so continue
				continue;
			}
		}
	}
	return 0;
}


// returns a char string based on type; type (0==directory path leading up to last element of path, 1==last element of path)
char *get_element(int type, const char *the_path, char *return_str)
{
	char path[BLOCK_SIZE + 1];
	strcpy(path, the_path);
	// parts_to_path will be 1 if the search path is similar to "/dir"; it will be 2 if the search path is similar to "/other/dir"
	int parts_to_path = count_chars(path, '/');
	// create an array of strings to contain all parts of the path
	char *path_parts[parts_to_path - 1];
	int i = 0;
	// tokenize the path and put the contents into a string array
	path_parts[i] = strtok(path, "/");
	while (path_parts[i++] != NULL) {
		path_parts[i] = strtok(NULL, "/");
	}
	// if the last element of path_parts is NULL, ignore it since an extra "/" may have been passed to path
	if ( path_parts[parts_to_path - 1] == NULL ) {
		--parts_to_path;
	}
	// if we just want the last element, return the last element
	if (type == 1) {
		strcpy(return_str, path_parts[parts_to_path - 1]);
		return return_str;
	} else { // return all the elements with "/" separation leading up to the last element
		//char dir_path[BLOCK_SIZE + 1];
		strcpy(return_str, "/");
		for (i = 0; i < (parts_to_path - 2); ++i) {
			strcat(return_str, path_parts[i]);
			strcat(return_str, "/");
		}
		strcat(return_str, path_parts[i]);
		return return_str;
	}	
}

// updates to a directory's file_to_inode_dict; returns -1 if there is a write error, 1 if the write would cause the directory block to surpass the BLOCK_SIZE limit, 0 if successful
static int add_dict_entry(char type, char *name, int inode_block, FILE *fd)
{
	char buf[BLOCK_SIZE + 1];
	// get the contents of the file
	char file_contents[BLOCK_SIZE + 1];
	fread(file_contents, BLOCK_SIZE, 1, fd);
	rewind(fd);
	char *parts[3]; // 3 because the file is split into 3 groupings based on the "{}" delimiter characters
	int i = 0;
	// tokenize the file contents by the "{}" delimiter characters; parts[1]: directory info, parts[2]: previous inodes in file; parts[3]: 0's at end of file
	parts[i] = strtok(file_contents, "{}");
	while (parts[i++] != NULL) {
		parts[i] = strtok(NULL, "{}");
	}
	
	// copy the info about the directory into a char array so we can update the link count
	char parts_0[BLOCK_SIZE + 1];
	// get the original directory info, updating the linkcount, and put it into the parts_0 char array
	char size[BLOCK_SIZE]; char uid[BLOCK_SIZE]; char gid[BLOCK_SIZE]; char mode[BLOCK_SIZE];
	char linkcount_str[BLOCK_SIZE]; int linkcount;
	char atime_str[BLOCK_SIZE]; char ctime_str[BLOCK_SIZE]; char mtime_str[BLOCK_SIZE];
	char fname_inode[BLOCK_SIZE]; //char inodes[BLOCK_SIZE];
	//char indirect[BLOCK_SIZE]; char location[BLOCK_SIZE];
	//char temp[10];
	// format will be for a directory
	// increment the linkcount for a directory because a new inode was added to its dict
	fscanf(fd, "%s %s %s %s %s %s %s %10c%i%*c %s", size, uid, gid, mode, atime_str, ctime_str, mtime_str, linkcount_str, &linkcount, fname_inode);
	// increment the linkcount to account for the new directory entry if the entry is a directory because the new directory's '..' will point to this directory
	if (type == 'd') {
		++linkcount;
	}
	sprintf(parts_0, "%s %s %s %s %s %s %s linkcount:%i, %s", size, uid, gid, mode, atime_str, ctime_str, mtime_str, linkcount, fname_inode);
	rewind(fd);	
	
	// add the directory's info back along with appropriate "{}" chars, and add the new inode entry to the end of the previous entries
	if (sprintf(buf, "%s {%s,%c:%s:%i}}", parts_0, parts[1], type, name, inode_block) > BLOCK_SIZE) {
		logmsg("ERROR:\tadd_dict_entry\tsprintf\tbuf too large");
		return 1;
	}
	if ( write_to_file(buf, fd) != 0 ) {
		logmsg("FAILURE:\tadd_dict_entry\twrite_to_file");
		return -1;
	}
	// successfully wrote the new directory inode dictionary entry to the directory block
	return 0;
}

// attemps to add a to a directory's file_to_inode_dict; returns -1 if there is a write error, 1 if the write would cause the directory block to surpass the BLOCK_SIZE limit, 0 if successful
static int add_to_dir_dict(char type, char *name, int inode_block, char *dir_path)
{
	// open the directory's file
	int dir_block_num = search_path(dir_path, 0);
	char dir_block[MAX_PATH_LENGTH + 1];
	sprintf(dir_block, "fusedata.%i", dir_block_num);
	FILE *fd = fopen(dir_block, "r+");
	update_time(0, 1, 1, 0, fd); // update the atime and ctime of this directory
	rewind(fd);
	// get the result of trying to add the entry
	int result = add_dict_entry(type, name, inode_block, fd);
	fclose(fd);
	return result;
}


// return a file's attribute; type (0==dir, 1==file), attr (0==linkcount, 1==size, 2==atime, 3==ctime, 4==mtime); return -1 if something goes wrong
static int get_file_attr(int type, int attr, FILE *fd)
{
	char temp[BLOCK_SIZE + 1];
	int links; int size; unsigned int a_time; unsigned int c_time; unsigned int m_time;

	rewind(fd);
	update_time(type, 1, 0, 0, fd);
	rewind(fd);
	
	// set the values for links, size, atime, ctime, and mtime
	if ( type == 0 ) { // parse through a directory formatted file descriptor
		fscanf(fd, "%6c%i%*c %s %s %s %6c%u%*c %6c%u%*c %6c%u%*c %10c%i", temp, &size, temp, temp, temp, temp, &a_time, temp, &c_time, temp, &m_time, temp, &links);
	} else { // parse through a file formatted file descriptor
		fscanf(fd, "%6c%i%*c %s %s %s %10c%i%*c %6c%u%*c %6c%u%*c %6c%u%*c", temp, &size, temp, temp, temp, temp, &links, temp, &a_time, temp, &c_time, temp, &m_time);
	}
	
	switch (attr)
	{
		case 0: // linkcount
			return links;
		case 1: // size
			return size;
		case 2: // atime
			return a_time;
		case 3: // ctime
			return c_time;
		case 4: // mtime
			return m_time;
		default:
			return -1;
	}
	return -1;
}



// ----------------------------------- FUNCTIONS FOR jb_init -------------------------------------

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

// ----------------------------------- FUNCTIONS FOR jb_init -------------------------------------





// ------------------------------------- FUSE FUNCTIONS -----------------------------------------


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
	// initialize the stat struct to all 0's because this function will set its values
	memset(stbuf, 0, sizeof(struct stat));
	// search_path will return -1 if the path has invalid directory references, 0 if the file does not exist, or a block_number if the file already exists
	int file_ref = search_path(path, 1);
	// if file_ref is less than 1 (i.e. 0 or -1) the file path does not exist
	if (file_ref < 1) {
		logmsg("ERROR:\tjb_create\tfile_ref\tENOENT");
		errno = ENOENT;
		return -errno;
	}
	// file_ref holds a number that is the fusedata block of the directory/file inode
	char fusedata_str[BLOCK_SIZE + 1];
	sprintf(fusedata_str, "fusedata.%i", file_ref);
	// open the fusedata.X file and determine if path resolves to a dir (2 '{' in its contents) or a file (1 '{' in its contents)
	FILE *fd = fopen(fusedata_str, "r+");
	char file_contents[BLOCK_SIZE + 1];
	fread(file_contents, BLOCK_SIZE, 1, fd);
	int num_curly = count_chars(file_contents, '{');
	mode_t mode; ino_t ino; nlink_t nlink; off_t size; time_t a_time; time_t c_time; time_t m_time;
	// get_file_attr; type (0==dir, 1==file), attr (0==linkcount, 1==size, 2==atime, 3==ctime, 4==mtime), fd==file_stream; return -1 if something goes wrong
	// find the stat *stbuf values depending on whether we're looking at a directory or a file
	if (num_curly == 2) { // if num_curly is 2, the path resolved to a directory
		mode = S_IFDIR | 0755;
		ino = file_ref;
		nlink = get_file_attr(0, 0, fd);
		size = get_file_attr(0, 1, fd);
		a_time = get_file_attr(0, 2, fd);
		c_time = get_file_attr(0, 3, fd);
		m_time = get_file_attr(0, 4, fd);
	} else { // the path is a file
		mode = S_IFREG | 0755;
		ino = file_ref;
		nlink = get_file_attr(1, 0, fd);
		size = get_file_attr(1, 1, fd);
		a_time = get_file_attr(1, 2, fd);
		c_time = get_file_attr(1, 3, fd);
		m_time = get_file_attr(1, 4, fd);
	}
	
	// if there was an error in getting any of the file attributes, set errno to ENOENT and return -errno
	if ( (nlink == -1) || (size == -1) || (a_time == -1) || (c_time == -1) || (m_time == -1) ) {
		fclose(fd);
		errno = ENOENT;
		return -errno;
	}
	
	// set the stat *stbuf values
	stbuf->st_ino = ino;
	stbuf->st_mode = mode;
	stbuf->st_nlink = nlink;
	stbuf->st_size = size;
	stbuf->st_atime = a_time;
	stbuf->st_ctime = c_time;
	stbuf->st_mtime = m_time;

	fclose(fd);

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
/*	previous code:
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
*/	


	int fd;
	
	// search_path will return -1 if the path has invalid directory references, 0 if the directory does not exist, or a block_number if the directory already exists
	int dir_ref = search_path(path, 1);
	// if dir_ref is less than 1 (i.e. 0 or -1) the dir path does not exist
	if (dir_ref < 1) {
		logmsg("ERROR:\tjb_create\tdir_ref\tENOENT");
		errno = ENOENT;
		return -errno;
	}
	// otherwise the dir_ref is the block number of the fusedata.X file that refers to the directory path's inode
	// open the fusedata.X block corresponding the directory
	char fusedata_inode[MAX_PATH_LENGTH + 1];
	sprintf(fusedata_inode, "fusedata.%i", dir_ref);
	fd = open(fusedata_inode, fi->flags);
	// if there was an error opening the directory's block file
	if (fd == -1)
		return -errno;
	// set the fuse_file_info file handle to the newly created and opened directory inode
	fi->fh = fd;
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
	/* previous code
	struct jb_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
	*/
	close(fi->fh);
	return 0;
}




/* Create a directory with the given name
 * Note that the mode argument may not have the type specification bits set, (i.e., S_ISDIR(mode) can be false).
 * To obtain the correct directory type bits, use "mode|S_IFDIR".
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
	// search_path will return -1 if the path has invalid directory references, 0 if the directory does not exist, or a block_number if the directory already exists
	int file_ref = search_path(path, 0);
	if (file_ref == -1) {
		logmsg("ERROR:\tjb_mkdir\tfile_ref\tENOENT");
		errno = ENOENT;
		return -errno;
	} else if (file_ref > 0) {
		logmsg("ERROR:\tjb_mkdir\tfile_ref\tEEXIST");
		errno = EEXIST;
		return -errno;
	}
	// if the directory does not already exist and we have a valid path to some directory preceedign it, create the directory
	// get the 1 free blocks for the directory inode; if there isn't an available inode, return an error
	unsigned int dir_block = next_free_block();
	if (dir_block == -1) {
		logmsg("ERROR:\tjb_mkdir\tdir_block\tEDQUOTE");
		errno = EDQUOT; // all available free blocks are used up
		return -errno;
	}
	// get the directory's name and the directory path leading up to that directory name
	char dir_name[BLOCK_SIZE + 1];
	char dir_path[BLOCK_SIZE + 1];
	get_element(1, path, dir_name); // dir_name = get_element(1, path, dir_name);
	get_element(0, path, dir_path); // dir_path = get_element(0, path, dir_path);
	// inode_dir_val: -1: failure in writing data, 1: new content on write to file exceeds block size, 0: successful write
	int inode_dir_val = add_to_dir_dict('d', dir_name, dir_block, dir_path);
	if (inode_dir_val == -1) {
		logmsg("FAILURE:\tjb_mkdir\tinode_dir_val\tEIO");
		errno = EIO;
		return -errno;
	} else if (inode_dir_val == 1) {
		logmsg("ERROR:\tjb_mkdir\tinode_dir_val\tEFBIG");
		errno = EFBIG;
		return -errno;
	}
	// the dir_block was successfully written to the directory
	
	// get the fusedata block number of the parent directory path (we know it exists based on the inital search_path called in this function)
	unsigned int parent_block = search_path(dir_path, 0);
	// create the new directory's structure and include the inode_dict for this dir and the parent dir
	if (create_dir(dir_block, parent_block) != 0) { // if there was an error creating the block, it was due to the fact that was an IO error with writing to fusedata.X
		logmsg("FAILURE:\tjb_mkdir\tcreate_dir\tEIO");
		errno = EIO;
		return -errno;
	}
	
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
	// val will continue a positive integer if from represents a valid pathname to an existing file
	int val = search_path(from, 1);
	// if val is 0 or -1, the path to a file was not valid
	if ( val < 1 ) {
		logmsg("FAILURE:\tjb_link\tval\tENOENT");
		errno = ENOENT;
		return -errno;
	}
	// otherwise, val now holds the integer of the inode file
	
	int to_val = search_path(to, 1);
	// if to_val is -1, the path was not valid; if it was a positive integer, the file already exists (so we don't overwrite)
	if ( to_val == -1 ) {
		logmsg("FAILURE:\tjb_link\tto_val\tENOENT");
		errno = ENOENT;
		return -errno;
	} else if ( to_val > 0 ) { // the file already exists
		logmsg("ERROR:\tjb_link\tto_val\tEEXIST");
		errno = EEXIST;
		return -errno;
	}
	// otherwise, to_val was 0 meaning the directory path is valid but the file did exist in that directory yet

	char to_dir[BLOCK_SIZE + 1];
	char to_name[BLOCK_SIZE + 1];
	// get the directory path and file name involved with to
	get_element(0, to, to_dir);
	get_element(1, to, to_name);
	
	// add a directory inode entry: type is a file 'f', the name is to_name (the last element of to), the inode number is the same one as from's inode (val), and the directory path is to_dir
	int dir_dict_return = add_to_dir_dict('f', to_name, val, to_dir);
	if ( dir_dict_return == -1 ) {
		logmsg("FAILURE:\tjb_link\tadd_to_dir_dict\tEIO");
		errno = EIO;
		return -errno;
	} else if ( dir_dict_return == 1) {
		logmsg("ERROR:\tjb_link\tadd_to_dir_dict\tEFBIG");
		errno = EFBIG;
		return -errno;
	}
	// other wise, the entry was entered successfully
	
	// increment the linkcount of the inode since a new directory entry now points to it
	// variables to be used in fscanf function of the file stream	
	char buf[BLOCK_SIZE + 1];
	char size[BLOCK_SIZE]; char uid[BLOCK_SIZE]; char gid[BLOCK_SIZE]; char mode[BLOCK_SIZE];
	char linkcount_str[BLOCK_SIZE]; int linkcount;
	char atime_str[BLOCK_SIZE]; char ctime_str[BLOCK_SIZE]; char mtime_str[BLOCK_SIZE];
	char indirect[BLOCK_SIZE]; char location[BLOCK_SIZE];
	char temp[10];
	// open the inode file
	char inode_file[BLOCK_SIZE + 1];
	sprintf(inode_file, "fusedata.%i", val);
	FILE *fd = fopen(inode_file, "r+");
	// update the time fields of the inode
	update_time(1, 1, 1, 0, fd);
	fread(temp, 1, 1, fd); rewind(fd);
	// get and increment the link count and write the data back to the file with the updated link count
	fscanf(fd, "%s %s %s %s %10c%i%*c %s %s %s %s %s", size, uid, gid, mode, linkcount_str, &linkcount, atime_str, ctime_str, mtime_str, indirect, location);
	++linkcount; // increment the linkcount to account for the new link to the file
	if (sprintf(buf, "%s %s %s %s linkcount:%i, %s %s %s %s %s", size, uid, gid, mode, linkcount, atime_str, ctime_str, mtime_str, indirect, location) > BLOCK_SIZE) {
		logmsg("ERROR:\tjb_link\tlinkcount++\tEFBIG");
		errno = EFBIG;
		return -errno;
	}
	rewind(fd);
	if ( write_to_file(buf, fd) != 0 ) {
		logmsg("FAILURE:\tjb_link\tlinkcount++\tEIO");
		errno = EIO;
		return -errno;
	}
	fread(temp, 1, 1, fd); rewind(fd);
	fclose(fd);

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
	
	// search_path will return -1 if the path has invalid directory references, 0 if the file does not exist, or a block_number if the file already exists
	int file_ref = search_path(path, 1);
	if (file_ref == -1) {
		logmsg("ERROR:\tjb_create\tfile_ref\tENOENT");
		errno = ENOENT;
		return -errno;
	} else if (file_ref > 0) {
		logmsg("ERROR:\tjb_create\tfile_ref\tEEXIST");
		errno = EEXIST;
		return -errno;
	}
	// if the file does not already exist and we have a valid path to some directory, create the file
	// get the 2 free blocks for the inode and actual file; if there isn't enough inodes, return an error
	int inode_block = next_free_block();
	if (inode_block == -1) {
		logmsg("ERROR:\tjb_create\tinode_block\tEDQUOTE");
		errno = EDQUOT; // all available free blocks are used up
		return -errno;
	}
	int file_block = next_free_block();
	if (file_block == -1) {
		logmsg("ERROR:\tjb_create\tfile_block\tEDQUOTE");
		errno = EDQUOT; // all available free blocks are used up
		add_free_block(inode_block); // inode_block was removed from the free block list, but since there isn't room to for the file block, the inode_block should be put back as free
		return -errno;
	}
	// create the inode data block and set it's location to point to the number of the file_block
	create_inode(inode_block, file_block);
	
	// get the file name and the directory path leading up to that file name
	char file_name[BLOCK_SIZE + 1];
	char dir_path[BLOCK_SIZE + 1];
	get_element(1, path, file_name); //file_name = get_element(1, path, file_name);
	get_element(0, path, dir_path); //dir_path = get_element(0, path, dir_path);
	// inode_dir_val: -1: failure in writing data, 1: new content on write to file exceeds block size, 0: successful write
	int inode_dir_val = add_to_dir_dict('f', file_name, inode_block, dir_path);
	if (inode_dir_val == -1) {
		logmsg("FAILURE:\tjb_create\tinode_dir_val\tEIO");
		errno = EIO;
		return -errno;
	} else if (inode_dir_val == 1) {
		logmsg("ERROR:\tjb_create\tinode_dir_val\tEFBIG");
		errno = EFBIG;
		return -errno;
	}
	// the inode block was successfully written to the directory
	
	// open the fusedata.X file corresponding to the newly created file
	char fusedata_inode[MAX_PATH_LENGTH + 1];
	sprintf(fusedata_inode, "fusedata.%i", inode_block);
	fd = open(fusedata_inode, fi->flags);
	// if there was an error opening the file
	if (fd == -1)
		return -errno;
	// set the fuse_file_info file handle to the newly created and opened file
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
	
	// search_path will return -1 if the path has invalid directory references, 0 if the file does not exist, or a block_number if the file already exists
	int file_ref = search_path(path, 1);
	// if file_ref is less than 1 (i.e. 0 or -1) the file path does not exist
	if (file_ref < 1) {
		logmsg("ERROR:\tjb_create\tfile_ref\tENOENT");
		errno = ENOENT;
		return -errno;
	}
	// otherwise the file_ref is the block number of the fusedata.X file that refers to the file path's inode
	// open the fusedata.X block corresponding the inode
	char fusedata_inode[MAX_PATH_LENGTH + 1];
	sprintf(fusedata_inode, "fusedata.%i", file_ref);
	fd = open(fusedata_inode, fi->flags);
	// if there was an error opening the file
	if (fd == -1)
		return -errno;
	// set the fuse_file_info file handle to the newly created and opened file inode
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
	/* previous code
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
	*/
	
	unsigned int free_blocks = 0;
	// unsigned int inodes = 0;
	
	int i;
	char fusedata_str[MAX_PATH_LENGTH + 1];
	char file_contents[BLOCK_SIZE + 1];
	FILE *fd;
	for (i = FREE_START; i <= FREE_END; ++i) {
		// open and read the contents of fusedata.X (one of the free block list files) into a buffer
		sprintf(fusedata_str, "fusedata.%i", i);
		fd = fopen(fusedata_str, "r+");
		fread(file_contents, BLOCK_SIZE, 1, fd);
		// the number of characters in the file represents the number of free blocks available in that file
		free_blocks += count_chars(file_contents, ',');
		fclose(fd);
	}
	
	stbuf->f_bsize = BLOCK_SIZE;
	stbuf->f_blocks = MAX_NUM_BLOCKS;
	stbuf->f_bfree = free_blocks;
	stbuf->f_bavail = free_blocks;
	// f_files = inodes; // parse through every directory and increment a counter if f:name:block_num is found in the filename_to_inode_dict
	stbuf->f_ffree = free_blocks;
	stbuf->f_namemax = MAX_FILENAME_LEN;
	
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
	
	// close the open file descriptor stored in fi
	close(fi->fh);

	return 0;
}




/* Clean up filesystem (called on filesystem exit)
 * The "private_data" comes from the return value of init
*/
void jb_destroy(void *private_data)
{
	// don't do anything ???????????????
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
