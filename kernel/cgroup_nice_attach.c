#include <linux/cgroup.h>
#include <linux/kernel.h>

/*
 * Default Android check for whether the current process is allowed to move a
 * task across cgroups, either because CAP_SYS_NICE is set or because the uid
 * of the calling process is the same as the moved task or because we are
 * running as root.
 */
int cgroup_nice_allow_attach(struct cgroup_subsys_state *css,
					struct cgroup_taskset *tset)
{
	const struct cred *cred = current_cred(), *tcred;
	struct task_struct *task;

	if (capable(CAP_SYS_NICE))
		return 0;

	cgroup_taskset_for_each_2(task, tset) {
		tcred = __task_cred(task);

		if (current != task && !uid_eq(cred->euid, tcred->uid) &&
		    !uid_eq(cred->euid, tcred->suid))
			return -EACCES;
	}

	return 0;
}

