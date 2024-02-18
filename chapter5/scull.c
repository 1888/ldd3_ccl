#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>     /* kmalloc() */
#include <linux/fs.h>       /* register_chrdev_region */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#ifdef SCULL_DEBUG
#include <linux/proc_fs.h>  /* proc file */
#include <linux/seq_file.h>  /* seqence file */
#endif
#include <linux/fcntl.h>    /* O_ACCMODE */
#include <linux/cdev.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,15,0)
    #include <asm/uaccess.h>    /* copy_*_user */
#else
    #include <linux/uaccess.h>    /* copy_*_user */
#endif

#include <linux/semaphore.h>
#include "scull.h"

/*
 * Our parameters which can be set at load time.
 */
int scull_major = SCULL_MAJOR;
int scull_minor = 0;
int scull_nr_devs = SCULL_NR_DEVS;  /* number of bare scull devices */
int scull_quantum = SCULL_QUANTUM;  /* the size of every quantum */
int scull_qset = SCULL_QSET;        /* the num of quantum for a quantum set */

module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

MODULE_LICENSE("Dual BSD/GPL");

struct scull_dev *scull_devices;    /* allocated in scull_init_module */

/*
 * Empty out the scull device; must be called with the device
 * semaphore held.
 */
int scull_trim(struct scull_dev *dev)
{
	struct scull_qset *next, *dptr;
	int qset = dev->qset;
	int i;

	/* call each memory area (4K) a quantum
	* a quantum set has 1000 quantums
	*/
	for (dptr = dev->data; dptr; dptr = next) {
		if (dptr->data) { // this quantum set is available
		for (i = 0; i < qset; i++)
			kfree(dptr->data[i]); // free each quantum
			kfree(dptr->data);
			dptr->data = NULL;
		}
		next = dptr->next;
		kfree(dptr);
	}
	dev->size = 0;
	dev->quantum = scull_quantum;
	dev->qset = scull_qset;
	dev->data = NULL;
	return 0;
}

#ifdef SCULL_DEBUG
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
int scull_read_procmem(char* buf, char** start, off_t offset,
        int count, int *eof, void *data)
{
	int i,j,len=0;
	int limit = count - 80; /* Don't print more than this */

	for (i = 0; i < scull_nr_devs && len <= limit; i++) {
		struct scull_dev *d = &scull_devices[i];
		struct scull_qset *qs = d->data;
		if (down_interruptible(&d->sem))
			return -ERESTARTSYS;

		len += sprintf(buf+len, "\nDevice %i: qset %i, q %i, sz %li\n",
					i, d->qset, d->quantum, d->size);

		for(; qs && len <= limit; qs = qs->next) {/* scan the list */
			len += sprintf(buf+len, " item at %p, qset at %p\n",
			qs, qs->data);
			if (qs->data && !qs->next) /* dump only the last item */
				for (j = 0; j < d->qset; j++) {
				if (qs->data[j])
					len += sprintf(buf + len, "\t%4i:%8p\n",
								j, qs->data[j]);
			}
		}
		up(&d->sem);
	}
	*eof = 1;
	return len;
}
#else
ssize_t scull_read_procmem (struct file *filp, char __user *ubuf, size_t count, loff_t *f_pos)
{
	int i,j;
	ssize_t len=0;
    int limit = count - 80; /* Don't print more than this */
	char* buf;
	ssize_t ret;

	printk(KERN_INFO "scull_read_procmem count=%lu, pos=%lld!\n", count, *f_pos);
	if (*f_pos > 0 )
		return 0; /* If the value is 0, end-of-file was reached (and no data was read). */

	buf = kzalloc(4096, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "scull_read_procmem allocate memory failed!\n");
		return -ENOMEM;
	}

	for (i = 0; i < scull_nr_devs && len <= limit; i++) {
		struct scull_dev *d = &scull_devices[i];
		struct scull_qset *qs = d->data;
		if (down_interruptible(&d->sem)) {
			ret = -ERESTARTSYS;
			goto free_buf;
		}

		len += sprintf(buf+len, "\nDevice %i: each qset has %i quantums, each quantum has %i bytes, "
			"total bytes in the device: %li\n", i, d->qset, d->quantum, d->size);

		for(; qs && len <= limit; qs = qs->next) {/* scan the list */
			len += sprintf(buf+len, " item at %p, qset at %p. quantums in this qset:\n",
						qs, qs->data);
			if (qs->data && !qs->next) /* dump only the last item */
				for (j = 0; j < d->qset; j++) {
					if (qs->data[j])
						len += sprintf(buf + len, "\tquantum[%4i] address: %p\n",
									j, qs->data[j]);
				}
		}
		up(&d->sem);
	}

	copy_to_user(ubuf, buf, len);
	*f_pos = len;
	ret = len;

free_buf:
	kfree(buf);
	printk(KERN_INFO "scull_read_procmem return %lu, pos = %lld!\n", ret, *f_pos);
	return ret;
}
#endif

/* The sfile argument can almost always be ignored. pos is an integer position indicating where the reading should start.
 * Since seq_file implementations typically step through a sequence of interesting items,
 * the position is often interpreted as a cursor pointing to the next item in the sequence.
 * The scull driver interprets each device as one item in the sequence,
 * so the incoming pos is simply an index into the scull_devices array.
 */ 
static void *scull_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= scull_nr_devs)
		return NULL; /* No more to read */
	return scull_devices + *pos;
}

/*
 * v is the iterator as returned from the previous call to start or next,
 * pos is the current position in the file
 * next should increment the value pointed to by pos
 * depending on how your iterator works, you might (though probably won’t) want to increment pos by more than one.
 */
static void *scull_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= scull_nr_devs)
		return NULL;
	return scull_devices + *pos;
}

/* When the kernel is done with the iterator, it calls stop to clean up
 * The scull implementation has no cleanup work to do, so its stop method is empty.
 */
static void scull_seq_stop(struct seq_file *s, void *v)
{
	/* Actually, there's nothing to do here */
}

/*
 * In between these calls, the kernel calls the show method to actually output something interesting to the user space. */
static int scull_seq_show(struct seq_file *s, void *v)
{
    /* Here, we finally interpret our “iterator” value, 
     * which is simply a pointer to a scull_dev structure.
     */
	struct scull_dev *dev = (struct scull_dev *) v;
	struct scull_qset *d;
	int i;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	seq_printf(s, "\nDevice %i: qset %i, q %i, sz %li\n",
			(int) (dev - scull_devices), dev->qset,
			dev->quantum, dev->size);
	for (d = dev->data; d; d = d->next) { /* scan the list */
		seq_printf(s, "  item at %p, qset at %p\n", d, d->data);
		if (d->data && !d->next) /* dump only the last item */
			for (i = 0; i < dev->qset; i++) {
				if (d->data[i])
					seq_printf(s, "    % 4i: %8p\n",
							i, d->data[i]);
			}
	}
	up(&dev->sem);
	return 0;
}

/*
 * Now that it has a full set of iterator operations, 
 * scull must package them up and connect them to a file in /proc 
 */
static struct seq_operations scull_seq_ops = {
	.start = scull_seq_start,
	.next  = scull_seq_next,
	.stop  = scull_seq_stop,
	.show  = scull_seq_show
}; 

/* create an open method that connects the file to the seq_file operations
 * */
static int scull_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scull_seq_ops);
}

/* open is the only file operation we must implement ourselves, 
 * so we can now set up our file_operations structure
 * Here we specify our own open method, but use the canned methods seq_read, seq_lseek, and seq_release for everything else
 */
static struct file_operations scull_seq_proc_ops = {
	.owner = THIS_MODULE,
	.open = scull_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};


#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
struct file_operations scull_proc_fops = {
	.owner = THIS_MODULE,
	.read = scull_read_procmem
};
#endif

/*
 * create two proc files in different ways
 * */
static void scull_create_proc(void)
{
	struct proc_dir_entry * proc_scullmem = NULL;
	struct proc_dir_entry * proc_scullseq = NULL;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	proc_scullmem = create_proc_read_entry("scullmem", 0 /* default mode */,
	NULL /* parent dir */, scull_read_procmem,
	NULL /* client data */);
#else
	proc_scullmem = proc_create("scullmem", 0, NULL, &scull_proc_fops);
#endif

	if(!proc_scullmem)
		printk(KERN_ERR "create scullmem failed!\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
	proc_scullseq = create_proc_entry("scullseq", 0, NULL);
	if (proc_scullseq)
		proc_scullseq->proc_fops = &scull_seq_proc_ops;
	else
		printk(KERN_ERR "create scullseq failed!\n");
#else
	proc_scullseq = proc_create("scullseq", 0, NULL, &scull_seq_proc_ops);
#endif
}

static void scull_remove_proc(void)
{
	/* no problem if it was not registered */
	remove_proc_entry("scullmem", NULL /* parent dir */);
	remove_proc_entry("scullseq", NULL);
}

#endif

/*
 * Open and Close
 */
int scull_open(struct inode *inode, struct file *filp)
{
	struct scull_dev *dev;

	/* identify which device is being opened.
	* the inode argument has the information we need in the form of
	* its i_cdev field, which contains the cdev structure we set up before.
	* we want the scull_dev structure that contians that cdev structure */
	dev = container_of(inode->i_cdev, struct scull_dev, cdev);
	filp->private_data = dev;

	/* now trim the length of the deivce to 0 if open was write-only */
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;

		scull_trim(dev);
		up(&dev->sem);
	}
	return 0;
}

int scull_release (struct inode *inode, struct file *filp)
{
	return 0;
}

/*
* @n: num of quantum_set
*
* if @dev->data is NULL, allocate the first quantum set.
* then move the pointer @n times to  point to the @n+1 quantum set
*/
struct scull_qset *scull_follow(struct scull_dev *dev, int n)
{
	struct scull_qset *qs = dev->data;
	/* allocate the first qset explicitly if need be */
	if (!qs) {
		qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
		if (qs == NULL)
			return NULL;
		memset(qs, 0, sizeof(struct scull_qset));
	}

	/* then follow the list */
	while (n--) {
		if (!qs->next) {
			qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
			if (qs->next == NULL)
				return NULL;
			memset(qs->next, 0, sizeof(struct scull_qset));
		}
		qs = qs->next;
		continue;
	}
	return qs;
}

/*
 * Data management: read and write
 */
ssize_t scull_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr; /* the first listitem */
	int quantum_size = dev->quantum; //bytes of a quantum
	int qset_size = dev->qset;  //num of quantum of a quantum set
	int item_size = quantum_size * qset_size; /* how many bytes in a listitem */
	int item, s_pos, q_pos, rest;
	ssize_t retval = 0;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	if (*f_pos >= dev->size)
		goto out;

	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;

	/* find listitem, qset index, and offset in the quantum */
	item = (long) *f_pos / item_size;// how many list items the current position is more than
	rest = (long) *f_pos % item_size;// the rest bytes in the last list item
	s_pos = rest / quantum_size; //the rest bytes can fully occupy s_pos quantuns
	q_pos = rest % quantum_size; //the last byte position in a quantum

	/* follow the list up to the right position (defined elsewhere) */
	dptr = scull_follow(dev, item); // find the right list item

	if (dptr == NULL || !dptr->data || !dptr->data[s_pos])
		goto out;

	/* read only up to the end of this quantum */
	if (count > quantum_size - q_pos)
		count = quantum_size - q_pos;

	if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;

out:
	up(&dev->sem);
	return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct scull_dev *dev = filp->private_data;
	struct scull_qset *dptr; /* the first listitem */
	int quantum_size = dev->quantum; //bytes of a quantum
	int qset_size = dev->qset;  //num of quantum of a quantum set
	int item_size = quantum_size * qset_size; /* how many bytes in a listitem(quantum set) */
	int item, s_pos, q_pos, rest;
	ssize_t retval = -ENOMEM;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	/* find listitem, qset index, and offset in the quantum */
	item = (long) *f_pos / item_size;// how many list items the current position is more than
	rest = (long) *f_pos % item_size;// the rest bytes in the last list item
	s_pos = rest / quantum_size; //the rest bytes can fully occupy s_pos quantuns
	q_pos = rest % quantum_size; //the last byte position in a quantum

	/* follow the list up to the right position (defined elsewhere) */
	dptr = scull_follow(dev, item); // find the right list item
	if (dptr == NULL)
		goto out;

	if (!dptr->data) {
		/* an quantum set has qset_size quantums*/
		dptr->data = kmalloc(qset_size * sizeof(char*), GFP_KERNEL);
		if (!dptr->data)
			goto out;
		memset(dptr->data, 0, qset_size * sizeof(char*));
	}

	if (!dptr->data[s_pos]) {
		/* each quantum has quantum_size bytes */
		dptr->data[s_pos] = kmalloc(quantum_size, GFP_KERNEL);
		if (!dptr->data[s_pos])
			goto out;
	}

	/* write only up to the end of this quantum */
	if (count > quantum_size - q_pos)
		count = quantum_size - q_pos;

	if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}

	*f_pos += count;
	retval = count;

	/* update the size */
	if (dev->size < *f_pos)
		dev->size = *f_pos;

out:
	up(&dev->sem);
	return retval;
}

static void faulty_write(void)
{
	PDEBUG("this is oops test by scull ioctrl. not an issue.\n");
	*(int*)0 = 0;
}

/*
 *  bits    meaning
 * dir: 31-30
 *          00 - no parameters: uses _IO macro
	        10 - read: _IOR
	        01 - write: _IOW
	        11 - read/write: _IOWR

 * size: 29-16	size of arguments

 * type: 15-8	ascii character supposedly
	unique to each driver

 * nr: 7-0	function #
 *      ioctl cmd
 * */
long scull_ioctl(struct file *filp,
        unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > SCULL_IOC_MAX)
		return -ENOTTY;

	PDEBUG("cmd 0x%08x.\n", cmd);
	switch(cmd) {
	case SCULL_IOC_MAKE_FAULTY_WRITE:
		faulty_write();
		break;

	default:
		PDEBUG("unknown cmd 0x%08x.\n", cmd);
		break;
	}

	return retval;
}
struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
//	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
	.unlocked_ioctl =    scull_ioctl,
	.open =     scull_open,
	.release =  scull_release,
};

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be carefull to work correctly even if some of the items
 * have not been initialized
 */
void scull_cleanup_module(void)
{
	int i;
	dev_t devno = MKDEV(scull_major, scull_minor);

	/* Get rid of our char dev entries */
	if (scull_devices) {
		for (i=0; i < scull_nr_devs; i++) {
			scull_trim(scull_devices + i);
			cdev_del(&scull_devices[i].cdev);
		}
		kfree(scull_devices);
	}
#ifdef SCULL_DEBUG
	scull_remove_proc();
#endif

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, scull_nr_devs);
	printk(KERN_WARNING "scull exit, major %d\n", scull_major);
}

/*
 * Set up the char_dev structure for this device.
 */
static void scull_setup_cdev(struct scull_dev *dev, int index)
{
    int err, devno = MKDEV(scull_major, scull_minor + index);

    cdev_init(&dev->cdev, &scull_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
        printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

int scull_init_module(void)
{
	int result, i;
	dev_t dev = 0;

	/* Get a range of minor numbers to work with, asking for a
	* dynamic major unless directed otherwise at load time.
	*/
	if (scull_major) {
		dev = MKDEV(scull_major, scull_minor);
		result = register_chrdev_region(dev, scull_nr_devs, "scull");
	}
	else {
		result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
		scull_major = MAJOR(dev);
	}

	if(result < 0) {
		printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
		return result;
	}

	/*
	* allocate the devices -- we can't have them static, as the number
	* can be specified at load time
	*/
	scull_devices = kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
	if (!scull_devices) {
		result = -ENOMEM;
		goto fail;
	}
	memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

	/* Initialize each device. */
	for (i = 0; i < scull_nr_devs; i++) {
		scull_devices[i].quantum = scull_quantum;
		scull_devices[i].qset = scull_qset;
		sema_init(&scull_devices[i].sem, 1); // initialized to 1 as mutex
		scull_setup_cdev(&scull_devices[i], i);
	}

#ifdef SCULL_DEBUG
	scull_create_proc();
#endif

	PDEBUG("probe done, major %d, minor %d, scull_nr_devs %d, each quantum has %d bytes, a quantum set has %d quantums\n",
		   scull_major, scull_minor, scull_nr_devs, scull_quantum, scull_qset);
	return 0;

fail:
	printk(KERN_ERR "scull: probe failed, major %d\n", scull_major);
	scull_cleanup_module();
	return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
