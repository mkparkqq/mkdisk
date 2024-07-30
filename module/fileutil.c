#include "fileutil.h"

#include <stdlib.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

void 
sprint_diskstat(const char *path, char *buf, size_t buflen)
{
	struct diskstat ds;
	if (diskstat(path, &ds) < 0) {
		snprintf(buf, buflen, "[diskstat] failed.");
		return;
	}
	snprintf(buf, buflen, "Available inodes: %d\nAvailable blocks: %d\nFilesystem block size: %d",
			ds.iavail, ds.bavail, ds.bsize);
}

const char *
futil_errstr(enum ERR_FUTIL err)
{
	if (ERR_FUTIL_OPEN == err)
		return "[futil] open";
	else if (ERR_FUTIL_SIZE_LIMIT == err)
		return "File size too big";
	else if (ERR_FUTIL_MALLOC == err)
		return "[malloc] Out of memory";
	else if (ERR_FUTIL_INVAL_MEM == err)
		return "Cannot access to the memory";
	else if (ERR_FUTIL_PARTIAL_READ == err)
		return "[futil] Partial read";
	else if (ERR_FUTIL_PARTIAL_WRITE == err)
		return "[futil] Partial write";
	else if (ERR_FUTIL_STATVFS == err)
		return "[futil] [statvfs]";
	else if (ERR_FUTIL_CLOSE == err)
		return "[futil] [close]";
	else if (ERR_FUTIL_REMOVE == err)
		return "[futil] [remove]";
	else if (ERR_FUTIL_RENAME == err)
		return "[futil] [remove]";
	else
		return "[futil] Undefined error";
}

int
file_exists(char *path)
{
	return access(path, F_OK) != -1;
}

/*
 * @return - success: 0, error(<0): enum ERR_FUTIL
 */
int 
diskstat(const char *path, struct diskstat *dstat)
{
	struct statvfs stat;
	memset(&stat, 0x00, sizeof(struct statvfs));
	memset(dstat, 0x00, sizeof(struct diskstat));

	if (statvfs(path, &stat) < 0)
		return ERR_FUTIL_STATVFS;

	dstat->bsize = stat.f_bsize;
	dstat->bavail = stat.f_bavail;
	dstat->iavail = stat.f_favail;
	dstat->namemax = stat.f_namemax;

	return 0;
}

int64_t
sizeof_file(const char *path) 
{
	FILE *fp = fopen(path, "rb");
	if (NULL == fp)
		return ERR_FUTIL_OPEN;
	int64_t fsize = 0L;
    fseek(fp, 0L, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);
	fclose(fp);
	return fsize;
}

/*
 * @param path - File path to load to memory(.buf).
 * @param buf - File data destination.
 * @param flen - File size will be set.
 * @return - Fail: error code(enum ERR_FUTIL)
 * 			 Success: 0
 */
int
read_file(const char *path, void *buf, int64_t flen)
{
	if (NULL == buf)
		return ERR_FUTIL_INVAL_MEM;

    FILE *fp = NULL;
    int64_t rlen;

    fp = fopen(path, "rb");
    if (fp == NULL)
		return ERR_FUTIL_OPEN;
    
    rlen = fread(buf, 1, flen, fp);

    if (rlen != flen) {
        fclose(fp);
		return ERR_FUTIL_PARTIAL_READ;
    }

    fclose(fp);

	return 0;
}

int
create_file(const char *path, void *buf, int64_t flen)
{
	FILE *file = fopen(path, "wb");
	if (NULL == file) {
		return ERR_FUTIL_OPEN;
	}

	// buffer -> file
    size_t written = fwrite(buf, 1, flen, file);
    if (written != flen) {
        fclose(file);
        return ERR_FUTIL_PARTIAL_WRITE;
    }

    if (0 != fclose(file)) {
        return ERR_FUTIL_CLOSE;
    }

    return 0;
}

int
delete_file(const char *path)
{
	if (0 == remove(path))
		return 0;
	return ERR_FUTIL_REMOVE;
}

int 
rename_file(const char *path_before, const char *path_after)
{
	if (0 == rename(path_before, path_after))
        return 0;
	return ERR_FUTIL_RENAME;
}
	
int
create_directory_if_not_exists(const char *dpath)
{
	 struct stat st = {0};

    // Check if the directory exists
    if (stat(dpath, &st) == -1) {
		// Create directory.
        if (mkdir(dpath, 0750) == 0) {
            return 0; // Success
        } else {
            return ERR_FUTIL_MKDIR;
        }
    } else
        return 0; // Directory already exists.
}
