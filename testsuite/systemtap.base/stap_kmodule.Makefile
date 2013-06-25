obj-m := stap_kmodule.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules
clean:
	rm -f *.mod.c stap_kmodule.ko *.o .*.cmd Modules.symvers
	rm -rf .tmp_versions
