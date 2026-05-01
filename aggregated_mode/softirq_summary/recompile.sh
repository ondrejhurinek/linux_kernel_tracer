#!/usr/bin/env bash

rm modules.order Module.symvers softirq_summary.ko softirq_summary.mod softirq_summary.mod.c softirq_summary.mod.o softirq_summary.o ../../common/trace_common/tp_lookup.o

make clean
make
