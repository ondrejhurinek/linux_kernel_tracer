obj-m += syscall_summary_mod.o
 
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)
KBUILD_EXTRA_SYMBOLS := /home/ondrej/Desktop/coursework/common/trace_tables/Module.symvers

syscall_summary_mod-y := syscall_summary.o ../../common/trace_common/tp_lookup.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
 
# obj-m         -> this object should become a loadable module
# -C $(KDIR)    -> use your running kernel's build system
# M=$(PWD)      -> build only this directory as an external module

