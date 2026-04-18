/*
 * POSIX directory compatibility for libdragon (N64)
 *
 * libdragon uses dir_findfirst/dir_findnext instead of POSIX opendir/readdir.
 * This shim provides the POSIX interface expected by PicoDrive's frontend.
 *
 * (C) 2026
 * This work is licensed under the terms of MAME license.
 * See COPYING file in the top-level directory.
 */
#ifndef N64_POSIX_COMPAT_H
#define N64_POSIX_COMPAT_H

#ifdef N64

/* Prevent libpicofe's posix.h from ever trying to do its own dispatch
 * (it doesn't know about N64).  We provide everything it would have
 * defined here, so when something later does
 * `#include "../libpicofe/posix.h"`, the guard short-circuits the
 * #error-on-unknown-platform path.  Paired with `-include` in the
 * N64 Makefile section so this compat shim is prepended to every TU. */
#define LIBPICOFE_POSIX_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>    /* real struct stat + stat()/mkdir() prototypes */
#include <dir.h>

/* Provide POSIX dirent types using libdragon's dir_t */
#ifndef DT_REG
#define DT_REG 1
#endif
#ifndef DT_DIR
#define DT_DIR 2
#endif
#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#endif
#ifndef DT_LNK
#define DT_LNK 3
#endif

struct dirent {
	char d_name[256];
	int  d_type;
};

typedef struct {
	char path[512];
	dir_t entry;
	struct dirent de;
	int first;
	int done;
} DIR;

static inline DIR *opendir(const char *path)
{
	DIR *d = (DIR *)malloc(sizeof(DIR));
	if (!d) return NULL;
	strncpy(d->path, path, sizeof(d->path) - 1);
	d->path[sizeof(d->path) - 1] = '\0';
	d->first = 1;
	d->done = 0;
	return d;
}

static inline struct dirent *readdir(DIR *d)
{
	if (!d || d->done) return NULL;

	int ret;
	if (d->first) {
		ret = dir_findfirst(d->path, &d->entry);
		d->first = 0;
	} else {
		ret = dir_findnext(d->path, &d->entry);
	}

	if (ret != 0) {
		d->done = 1;
		return NULL;
	}

	strncpy(d->de.d_name, d->entry.d_name, sizeof(d->de.d_name) - 1);
	d->de.d_name[sizeof(d->de.d_name) - 1] = '\0';
	d->de.d_type = d->entry.d_type;
	return &d->de;
}

static inline int closedir(DIR *d)
{
	if (d) free(d);
	return 0;
}

/* Stub for readlink - not available on N64 */
static inline int readlink(const char *p, char *d, int s) { return -1; }

/* getcwd stub */
static inline char *getcwd(char *buf, int size)
{
	if (buf && size > 4) {
		strcpy(buf, "sd:/");
		return buf;
	}
	return NULL;
}

/* stat()/mkdir() prototypes come from <sys/stat.h> above; real stubs
 * that always return -1 are provided out-of-line in plat.c so we match
 * libdragon's declared prototypes exactly (mode_t, off_t, etc.). */

/* access() stub */
#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0
#endif

static inline int access(const char *path, int mode)
{
	/* Try to open the file to check existence */
	FILE *f = fopen(path, "rb");
	if (f) { fclose(f); return 0; }
	return -1;
}

/* scandir/alphasort stubs */
static inline int alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

static inline int scandir(const char *dirp, struct dirent ***namelist,
	int (*filter)(const struct dirent *),
	int (*compar)(const struct dirent **, const struct dirent **))
{
	DIR *d = opendir(dirp);
	if (!d) return -1;

	struct dirent *de;
	int count = 0, alloc = 64;
	struct dirent **list = (struct dirent **)malloc(alloc * sizeof(struct dirent *));
	if (!list) { closedir(d); return -1; }

	while ((de = readdir(d)) != NULL) {
		if (filter && !filter(de))
			continue;
		if (count >= alloc) {
			alloc *= 2;
			struct dirent **tmp = (struct dirent **)realloc(list, alloc * sizeof(struct dirent *));
			if (!tmp) break;
			list = tmp;
		}
		list[count] = (struct dirent *)malloc(sizeof(struct dirent));
		if (!list[count]) break;
		memcpy(list[count], de, sizeof(struct dirent));
		count++;
	}
	closedir(d);

	if (compar && count > 1) {
		/* Simple insertion sort for small lists */
		for (int i = 1; i < count; i++) {
			struct dirent *tmp = list[i];
			int j = i - 1;
			while (j >= 0 && compar((const struct dirent **)&list[j],
			                        (const struct dirent **)&tmp) > 0) {
				list[j + 1] = list[j];
				j--;
			}
			list[j + 1] = tmp;
		}
	}

	*namelist = list;
	return count;
}

/* unlink stub */
static inline int unlink(const char *path) { return remove(path); }

/* usleep stub */
static inline int usleep(unsigned int us) { return 0; }

#endif /* N64 */
#endif /* N64_POSIX_COMPAT_H */
