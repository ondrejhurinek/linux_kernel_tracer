#!/usr/bin/env bash

rm modules.order Module.symvers irq_summary_event.ko irq_summary_event.mod irq_summary_event.mod.c irq_summary_event.mod.o irq_summary_event.o

make clean
make
