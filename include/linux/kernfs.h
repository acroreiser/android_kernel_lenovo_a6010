/*
 * kernfs.h - pseudo filesystem decoupled from vfs locking
 *
 * This file is released under the GPLv2.
 */

#ifndef __LINUX_KERNFS_H
#define __LINUX_KERNFS_H

#include <linux/kernel.h>
#include <linux/err.h>

struct sysfs_dirent;

#ifdef CONFIG_SYSFS

struct sysfs_dirent *kernfs_create_link(struct sysfs_dirent *parent,
					const char *name,
					struct sysfs_dirent *target);
void kernfs_remove(struct sysfs_dirent *sd);
int kernfs_remove_by_name_ns(struct sysfs_dirent *parent, const char *name,
			     const void *ns);

#else	/* CONFIG_SYSFS */

static inline struct sysfs_dirent *
kernfs_create_link(struct sysfs_dirent *parent, const char *name,
		   struct sysfs_dirent *target)
{ return ERR_PTR(-ENOSYS); }

static inline void kernfs_remove(struct sysfs_dirent *sd) { }

static inline int kernfs_remove_by_name_ns(struct sysfs_dirent *parent,
					   const char *name, const void *ns)
{ return -ENOSYS; }

#endif	/* CONFIG_SYSFS */

static inline int kernfs_remove_by_name(struct sysfs_dirent *parent,
					const char *name)
{
	return kernfs_remove_by_name_ns(parent, name, NULL);
}

#endif	/* __LINUX_KERNFS_H */
