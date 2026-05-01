#!/usr/bin/env bash

rm modules.order Module.symvers irq_summary.ko irq_summary.mod irq_summary.mod.c irq_summary.mod.o irq_summary.o ../../common/trace_common/tp_lookup.o

make clean
make
