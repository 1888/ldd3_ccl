#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
#include <sys/ioctl.h> /* _IO */
#include <fcntl.h> /* O_RDWR */

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

#define SCULL_DEVICE "/dev/scull"
#define SCULL_DEVICE_SIZE sizeof(SCULL_DEVICE)

int main(int argc, char **argv)
{
    char dev_node[SCULL_DEVICE_SIZE];
    char device_nr = 0;
    int fd;
    int retval;

    if (argc == 2)
        device_nr = atoi(argv[1]);

    memset(dev_node, 0, SCULL_DEVICE_SIZE);
    sprintf(dev_node,"%s%d", SCULL_DEVICE, device_nr);
    printf("make oops on %s\n", dev_node);

    fd = open(dev_node, O_RDWR);
    if (fd < 0){
        printf("open file failed!\n");
        return -1;
    }

    retval = ioctl(fd, SCULL_IOC_MAKE_FAULTY_WRITE,NULL);

    return retval;
}
