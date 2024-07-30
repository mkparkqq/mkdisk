/*
 * 2024-07-18 Minkeun Park.
 * File utility module for t-storage program.
 */

#ifndef _FILEUTIL_H_
#define _FILEUTIL_H_

#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>

enum ERR_FUTIL {
	ERR_FUTIL_OPEN = -1,
	ERR_FUTIL_SIZE_LIMIT = -2,
	ERR_FUTIL_MALLOC = -3,
	ERR_FUTIL_INVAL_MEM = -4,
	ERR_FUTIL_PARTIAL_READ = -5,
	ERR_FUTIL_PARTIAL_WRITE = -6,
	ERR_FUTIL_STATVFS = -7,
	ERR_FUTIL_CLOSE = -8,
	ERR_FUTIL_REMOVE = -9,
	ERR_FUTIL_RENAME = -10,
	ERR_FUTIL_MKDIR = -11
};

struct diskstat {
	unsigned long  bsize;     // Filesystem block size 
	fsblkcnt_t     bavail;    // Number of free blocks 
							  // for unprivileged users 
	fsfilcnt_t     iavail;    // Number of free inodes 
							  // for unprivileged users 
	unsigned long  namemax;   // Maximum filename length
};

const char *futil_errstr(enum ERR_FUTIL err);

void sprint_diskstat(const char *path, char *buf, size_t buflen);
int file_exists(char *);
int diskstat(const char *path, struct diskstat *);
int64_t sizeof_file(const char *path);
int read_file(const char *path, void *buf, int64_t flen);
int create_file(const char *path, void *buf, int64_t flen);
int delete_file(const char *path);
int rename_file(const char *path_before, const char *path_after);
int create_directory_if_not_exists(const char *dpath);

#endif
