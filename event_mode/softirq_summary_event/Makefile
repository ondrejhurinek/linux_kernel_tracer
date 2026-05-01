# means to build loadable kernel moodul from .c file
obj-m += softirq_summary_event_mod.o
# adds include path so that #include <mytracer_trace.h> can be found
ccflags-y += -I$(src)

# points to the kernel build directory for my currently running kernel version
KDIR := /lib/modules/$(shell uname -r)/build
# my current directory
PWD  := $(shell pwd)

softirq_summary_event_mod-y := softirq_summary_event.o ../../common/trace_common/tp_lookup.o

# all -> compile from kdir, pwd variables
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
# clean module build outputs
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
