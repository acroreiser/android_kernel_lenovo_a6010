/*
 * kernel/palloc.c
 *
 * Physical driven User Space Allocator info for a set of tasks.
 */

#include <linux/types.h>
#include <linux/cgroup.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/palloc.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/bitmap.h>
#include <linux/module.h>

/*
 * Check if a page is compliant to the policy defined for the given vma
 */
#ifdef CONFIG_CGROUP_PALLOC

#define MAX_LINE_LEN (6*128)
/*
 * Types of files in a palloc group
 * FILE_PALLOC - contain list of palloc bins allowed
*/
typedef enum {
	FILE_PALLOC,
} palloc_filetype_t;

/*
 * Top level palloc - mask initialized to zero implying no restriction on
 * physical pages
*/

static struct palloc top_palloc;

/* Retrieve the palloc group corresponding to this cgroup container */
struct palloc *cgroup_ph(struct cgroup *cgrp)
{
	return container_of(cgroup_subsys_state(cgrp, palloc_subsys_id),
			    struct palloc, css);
}

struct palloc * ph_from_subsys(struct cgroup_subsys_state * subsys)
{
	return container_of(subsys, struct palloc, css);
}

/*
 * Common write function for files in palloc cgroup
 */
static int update_bitmask(unsigned long *bitmap, const char *buf, int maxbits)
{
	int retval = 0;

	if (!*buf)
		bitmap_clear(bitmap, 0, maxbits);
	else
		retval = bitmap_parselist(buf, bitmap, maxbits);

	return retval;
}


static int palloc_file_write(struct cgroup *cgrp, struct cftype *cft,
			     const char *buf)
{
	int retval = 0;
	struct palloc *ph = cgroup_ph(cgrp);

	switch (cft->private) {
	case FILE_PALLOC:
		retval = update_bitmask(ph->cmap, buf, palloc_bins());
		printk(KERN_INFO "Bins : %s\n", buf);
		break;
	default:
		retval = -EINVAL;
		break;
	}

	return retval;
}

static ssize_t palloc_file_read(struct cgroup *cgrp,
				struct cftype *cft,
				struct file *file,
				char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct palloc *ph = cgroup_ph(cgrp);
	char *page;
	ssize_t retval = 0;
	char *s;

	if (!(page = (char *)__get_free_page(GFP_TEMPORARY)))
		return -ENOMEM;

	s = page;

	switch (cft->private) {
	case FILE_PALLOC:
		s += bitmap_scnlistprintf(s, PAGE_SIZE, ph->cmap, palloc_bins());
		printk(KERN_INFO "Bins : %s\n", s);
		break;
	default:
		retval = -EINVAL;
		goto out;
	}
	*s++ = '\n';

	retval = simple_read_from_buffer(buf, nbytes, ppos, page, s - page);
out:
	free_page((unsigned long)page);
	return retval;
}


/*
 * struct cftype: handler definitions for cgroup control files
 *
 * for the common functions, 'private' gives the type of the file
 */
static struct cftype files[] = {
	{
		.name = "bins",
		.read = palloc_file_read,
		.write_string = palloc_file_write,
		.max_write_len = MAX_LINE_LEN,
		.private = FILE_PALLOC,
	},
	{ }	/* terminate */
};

/*
 * palloc_create - create a palloc group
 */
static struct cgroup_subsys_state *palloc_create(struct cgroup *cgrp)
{
        struct palloc * ph_child;
        struct palloc * ph_parent;

        printk(KERN_INFO "Creating the new cgroup - %p\n", cgrp);

        if (!cgrp->parent) {
                return &top_palloc.css;
        }
        ph_parent = cgroup_ph(cgrp->parent);

        ph_child = kmalloc(sizeof(struct palloc), GFP_KERNEL);
        if(!ph_child)
                return ERR_PTR(-ENOMEM);

        bitmap_clear(ph_child->cmap, 0, MAX_PALLOC_BINS);
        return &ph_child->css;
}


/*
 * Destroy an existing palloc group
 */
static void palloc_destroy(struct cgroup *cgrp)
{
        struct palloc *ph = cgroup_ph(cgrp);
        printk(KERN_INFO "Deleting the cgroup - %p\n",cgrp);
        kfree(ph);
}

struct cgroup_subsys palloc_subsys = {
	.name = "palloc",
	.css_alloc = palloc_create,
	.css_free = palloc_destroy,
	.subsys_id = palloc_subsys_id,
	.base_cftypes = files,
};

#endif /* CONFIG_CGROUP_PALLOC */
