obj-m += trace_tables.o
 
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
 
# obj-m         -> this object should become a loadable module
# -C $(KDIR)    -> use your running kernel's build system
# M=$(PWD)      -> build only this directory as an external module
