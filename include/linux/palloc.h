#ifndef _LINUX_PALLOC_H
#define _LINUX_PALLOC_H

/*
 * kernel/palloc.h
 *
 * PHysical memory aware allocator
 */

#include <linux/types.h>
#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/mm.h>

#ifdef CONFIG_CGROUP_PALLOC

struct palloc {
	struct cgroup_subsys_state css;
	COLOR_BITMAP(cmap);
};

/* Retrieve the palloc group corresponding to this cgroup container */
struct palloc *cgroup_ph(struct cgroup *cgrp);

/* Retrieve the palloc group corresponding to this subsys */
struct palloc * ph_from_subsys(struct cgroup_subsys_state * subsys);

/* return #of palloc bins */
int palloc_bins(void);

#endif /* CONFIG_CGROUP_PALLOC */

#endif /* _LINUX_PALLOC_H */
