obj-m := memory1_module.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -f *.mod.c memory1_module.ko *.o .*.cmd Modules.symvers
	rm -rf .tmp_versions
