obj-m += irq_summary_mod.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

irq_summary_mod-y := irq_summary.o ../../common/trace_common/tp_lookup.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
