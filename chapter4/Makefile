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

	# Comment/uncomment the following line to disable/enable debugging
	DEBUG = y

	# Add your debugging flag (or not) to CFLAGS
ifeq ($(DEBUG),y)
	#DEBFLAGS = -O -save-temps -g -DSCULL_DEBUG # "-O" is needed to expand inlines
	DEBFLAGS = -O -g -DSCULL_DEBUG # "-O" is needed to expand inlines
else
	DEBFLAGS = -O2
endif

ccflags-y += $(DEBFLAGS)

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
    obj-m := scull.o
endif

