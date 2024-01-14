#ifndef _SCULL_H_
#define _SCULL_H_

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0 /* dynamic major by default */
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4 /* scull0 to scull3 */
#endif

/*
 * The bare device is a variable-length region of memory.
 * Use a linked list of indirect blocks.
 *
 * "scull_dev->data" points to an array of pointers, each
 * pointer refers to a memory area of SCULL_QUANTUM bytes.
 *
 * The array (quantum-set) is SCULL_QSET long.
 */
#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4000
#endif

#ifndef SCULL_QSET
#define SCULL_QSET 1000
#endif

#undef PDEBUG   /* undef it, just in case */
//#define SCULL_DEBUG
#ifdef SCULL_DEBUG
#ifdef __KERNEL__
    /* debugging is on in kernel space */
    #define PDEBUG(fmt, args...) printk(KERN_DEBUG "scull: " fmt, ## args)
#else
    /* debugging is on in user space */
    #define PDEBUG(fmt, args...) fprintf(stderr, fmt, ##args)
#endif

#else
    #define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

#undef PDEBUGG
#define PDEBUGG(fmt, args...) /* nothing: it's a placeholder */

/*
 * Representation of scull quantum sets.
 * @data: an array of pointers, which point to a quantum
 * @next: point to the next scull_qset
 *
 * the size of @data is defined by scull_dev->qset (default SCULL_QUANTUM 4000). 
 * the size of each quantum is defined by scull_dev->quantum (default SCULL_QSET 1000).
 * so the total size of a quantum_set is scull_dev->qset * scull_dev->quantum.
 */
struct scull_qset {
    void **data;
    struct scull_qset *next;
};

/*
* @data: pointer to first quantum_set, multi quantum_set linked as a single list
* @quantum: bytes of a quantum
* @qset: how many quantum(s) in a quantum_set
* @size: the total size of the data stored in this device
*/
struct scull_dev {
    struct scull_qset *data;
    int quantum;
    int qset;
    unsigned long size;         /* amount of data stored here */
    unsigned long access_key;   /* used by sculluid and scullpriv */
    struct mutex mutex;         /* mutual exclusion semaphore */
    struct cdev cdev;           /* Char device structure */
};
 
/*
 * Ioctl definitions
 */

/* Use 'k' as magic number */
#define SCULL_IOC_MAGIC  'c'
/* Please use a different 8-bit number in your code */

/* If you are adding new ioctl's to the kernel, you should use the _IO
 * macros defined in <linux/ioctl.h>:
 *   _IO    an ioctl with no parameters
 *   _IOW   an ioctl with write parameters (copy_from_user)
 *   _IOR   an ioctl with read parameters  (copy_to_user)
 *   _IOWR  an ioctl with both write and read parameters.
 */
#define SCULL_IOC_MAKE_FAULTY_WRITE    _IO(SCULL_IOC_MAGIC, 0)
/* define the max command of ioctrl. 
 * here is the last one is 0 in MAKE_FAULTY_WRITE 
 */
#define SCULL_IOC_MAX    0

#endif
