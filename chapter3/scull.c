#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>     /* kmalloc() */
#include <linux/fs.h>       /* register_chrdev_region */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/fcntl.h>    /* O_ACCMODE */
#include <linux/cdev.h>

#include <asm/uaccess.h>    /* copy_*_user */

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

/*
 * Open and Close
 */
int scull_open(struct inode *inode, struct file *filp)
{
    struct scull_dev *dev;

    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    filp->private_data = dev;

    /* now trim to 0 the length of the deivce if open was write-only */
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (mutex_lock_interruptible(&dev->mutex))
            return -ERESTARTSYS;
        scull_trim(dev);
        mutex_unlock(&dev->mutex);
    }
    return 0;
}

int scull_release (struct inode *inode, struct file *filp)
{
    return 0;
}

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

    if (mutex_lock_interruptible(&dev->mutex))
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
    mutex_unlock(&dev->mutex);
    return retval;
}

ssize_t scull_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr; /* the first listitem */
    int quantum_size = dev->quantum; //bytes of a quantum
    int qset_size = dev->qset;  //num of quantum of a quantum set
    int item_size = quantum_size * qset_size; /* how many bytes in a listitem */
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;

    if (mutex_lock_interruptible(&dev->mutex))
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
    mutex_unlock(&dev->mutex);
    return retval;
}

struct file_operations scull_fops = {
	.owner =    THIS_MODULE,
//	.llseek =   scull_llseek,
	.read =     scull_read,
	.write =    scull_write,
//	.unlocked_ioctl =    scull_ioctl,
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
    dev->cdev.ops = &scull_fops;
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
    } else {
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
        mutex_init(&scull_devices[i].mutex);
        scull_setup_cdev(&scull_devices[i], i);
    }
 
    printk(KERN_INFO "scull: probe done, major %d, minor %d, scull_nr_devs %d, quantum %d, quantum set %d\n", scull_major, scull_minor, scull_nr_devs, scull_quantum, scull_qset);
    return 0;

fail:
    printk(KERN_ERR "scull: probe failed, major %d\n", scull_major);
    scull_cleanup_module();
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
