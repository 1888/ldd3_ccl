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
 */
struct scull_qset {
    void **data;
    struct scull_qset *next;
};

struct scull_dev {
    struct scull_qset *data;    /* pointer to first quantum set */
    int quantum;                /* the current quantum size */
    int qset;                   /* the current array size */
    unsigned long size;         /* amount of data stored here */
    unsigned long access_key;   /* used by sculluid and scullpriv */
    struct mutex mutex;         /* mutual exclusion semaphore */
    struct cdev cdev;           /* Char device structure */
};
 
#endif
