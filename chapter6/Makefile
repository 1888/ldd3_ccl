# Comment/uncomment the following line to disable/enable debugging
DEBUG = y

# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	DEBFLAGS = -O -g -DSCULL_DEBUG # "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

# CFLAGS was changed in "/mnt/hgfs/01.sharedfolder/ldd3_kernel_3.5.0/chapter4/Makefile". 
# Fix it to use ccflags-y
# actually use EXTRA_CFLAGS is also OK.
# but there is a question: if put below line just before target modules, 
# below line will not work. don't know why.
ccflags-y += $(DEBFLAGS)
#EXTRA_CFLAGS += $(DEBFLAGS)

# To build modules outside of the kernel tree, we run "make"
# in the kernel source tree; the Makefile these then includes this
# Makefile once again.
# This conditional selects whether we are being included from the
# kernel Makefile or not.
ifeq ($(KERNELRELEASE),)

    # Assume the source tree is where the running kernel was built
    # You should set KERNELDIR in the environment if it's elsewhere
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    # The current directory is passed to sub-makes as argument
    PWD := $(shell pwd)
	INCLUDEDIR = $(KERNELDIR)/include
	ccflags-y += -I$(INCLUDEDIR)

modules:
	echo $(ccflags-y)
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions Module.symvers modules.order .cache.mk

.PHONY: modules modules_install clean

else
    # called from kernel build system: just declare what our modules are
    obj-m := jit.o
endif


