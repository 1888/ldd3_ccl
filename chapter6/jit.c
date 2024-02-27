#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timer.h>

#define JIT_CURRENT_TIME "currentime"

/*
 * This file, on the other hand, returns the current time forever
 */
int jit_currentime_show(struct seq_file *m, void *v)
{
	struct timeval tv1;
	struct timespec tv2;
	unsigned long j1;
	u64 j2;

	/* get them four */
	j1 = jiffies;
	j2 = get_jiffies_64();
	do_gettimeofday(&tv1);
	tv2 = current_kernel_time();

	/* print */
	seq_printf(m, "jiffies 0x%08lx, jiffies_64 0x%016Lx, do_gettimeofday %10i.%06i"
		       "current_kernel_time %40i.%09i\n",
		       j1, j2,
		       (int) tv1.tv_sec, (int) tv1.tv_usec,
		       (int) tv2.tv_sec, (int) tv2.tv_nsec);
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
	proc_create_data(JIT_CURRENT_TIME, 0, NULL, &jit_currentime_fops, NULL);

	return 0; /* success */
}

void __exit jit_cleanup(void)
{
	remove_proc_entry(JIT_CURRENT_TIME, NULL);
}

module_init(jit_init);
module_exit(jit_cleanup);

MODULE_AUTHOR("Alessandro Rubini");
MODULE_LICENSE("Dual BSD/GPL");
