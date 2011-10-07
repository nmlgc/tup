#include "open_notify.h"
#include "bsd/queue.h"
#include "tup/file.h"
#include "tup/db.h"
#include <stdio.h>
#include <sys/stat.h>

struct finfo_list {
	LIST_ENTRY(finfo_list) list;
	struct file_info *finfo;
};
LIST_HEAD(,finfo_list) finfo_list_head;

/* The stat() wrapper will call open_notify(), which uses stat(), so we have
 * to call the real version to avoid an infinite recursion.
 */
int __real_stat(const char *path, struct stat *buf);

int open_notify_push(struct file_info *finfo)
{
	struct finfo_list *flist;

	flist = malloc(sizeof *flist);
	if(!flist) {
		perror("malloc");
		return -1;
	}
	flist->finfo = finfo;
	LIST_INSERT_HEAD(&finfo_list_head, flist, list);
	return 0;
}

int open_notify_pop(struct file_info *finfo)
{
	struct finfo_list *flist;
	if(LIST_EMPTY(&finfo_list_head)) {
		fprintf(stderr, "tup internal error: finfo_list is empty.\n");
		return -1;
	}
	flist = LIST_FIRST(&finfo_list_head);
	if(flist->finfo != finfo) {
		fprintf(stderr, "tup internal error: open_notify_pop() element is not at the head of the list.\n");
		return -1;
	}
	LIST_REMOVE(flist, list);
	free(flist);
	return 0;
}

int open_notify(enum access_type at, const char *pathname)
{
	/* For the parser: manually keep track of file accesses, since we
	 * don't run the UDP server for the parsing stage in win32.
	 */
	if(!LIST_EMPTY(&finfo_list_head)) {
		struct finfo_list *flist;
		struct stat buf;
		char fullpath[PATH_MAX];
		int cwdlen;
		int pathlen;

		if(getcwd(fullpath, sizeof(fullpath)) != fullpath) {
			perror("getcwd");
			return -1;
		}
		cwdlen = strlen(fullpath);
		pathlen = strlen(pathname);
		if(cwdlen + pathlen + 2 >= (signed)sizeof(fullpath)) {
			fprintf(stderr, "tup internal error: max pathname exceeded.\n");
			return -1;
		}
		fullpath[cwdlen] = PATH_SEP;
		memcpy(fullpath + cwdlen + 1, pathname, pathlen);
		fullpath[cwdlen + pathlen + 1] = 0;

		/* If the stat fails, or if the stat works and we know it
		 * is a directory, don't actually add the dependency. We
		 * want failed stats for ghost nodes, and all successful
		 * file accesses.
		 */
		if(__real_stat(pathname, &buf) < 0 || !S_ISDIR(buf.st_mode)) {
			flist = LIST_FIRST(&finfo_list_head);
			if(handle_open_file(at, fullpath, flist->finfo, DOT_DT) < 0)
				return -1;
		}
	}
	return 0;
}