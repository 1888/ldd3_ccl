#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>

/* JIT stands for "Just In Time" */
#define JIT "jit"
#define JIT_CURRENT_TIME "currentime"

static struct proc_dir_entry *jit_folder;

/*
 * This file, on the other hand, returns the current time forever
 */
int jit_currentime_show(struct seq_file *m, void *v)
{
	struct timeval tv1;
	struct timespec tv2;
	unsigned long j1;
	u64 j2;

	/*
	 * The counter is initialized to 0 at system boot, so it represents the
	 * number of clock ticks since last boot. The counter is a 64-bit
	 * variable (even on 32-bit architectures) and is called jiffies_64.
	 * However, driver writers normally access the jiffies variable, an
	 * unsigned long that is the same as either jiffies_64 or its least
	 * significant bits. Using jififes is usually preferred bacause it is
	 * faster, and access to the 64-bit jiffies_64 value are not necessary
	 * atomic on 32-bit architectures).
	 */
	j1 = jiffies;
	j2 = get_jiffies_64();
	do_gettimeofday(&tv1);
	tv2 = current_kernel_time();

	/* print */
	seq_printf(m, "jiffies\t\t 0x%016lx, do_gettimeofday\t\t %10i.%06i\n"
		       "jiffies_64\t 0x%016Lx, current_kernel_time\t %10i.%09i\n",
		       j1, (int) tv1.tv_sec, (int) tv1.tv_usec,
		       j2, (int) tv2.tv_sec, (int) tv2.tv_nsec);
	return 0;
}

static int jit_currentime_open(struct inode *inode, struct file *file)
{
	return single_open(file, jit_currentime_show, NULL);
}

static const struct file_operations jit_currentime_fops = {
	.open		= jit_currentime_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

int __init jit_init(void)
{
	int ret;
	struct proc_dir_entry *current_time;
	jit_folder = proc_mkdir(JIT, NULL);
	if (!jit_folder) {
		pr_err("Failed to create /proc/jit folder\n");
		return -ENOMEM;
	}

	current_time = proc_create_data(JIT_CURRENT_TIME, 0666, jit_folder, &jit_currentime_fops, NULL);
	if(!current_time) {
		pr_err("Failed to create /proc/jit/current_time\n");
		ret = -ENOMEM;
		goto remove_jit;
	}

	pr_info("insmod /proc/jit\n");
	return 0; /* success */

remove_jit:
	remove_proc_entry(JIT, NULL);
	return ret;
}

void __exit jit_cleanup(void)
{
	pr_info("remove /proc/jit/current_time\n");
	remove_proc_entry(JIT_CURRENT_TIME, jit_folder);
	pr_info("remove /proc/jit\n");
	remove_proc_entry(JIT, NULL);
}

module_init(jit_init);
module_exit(jit_cleanup);

MODULE_AUTHOR("Alessandro Rubini");
MODULE_LICENSE("Dual BSD/GPL");
