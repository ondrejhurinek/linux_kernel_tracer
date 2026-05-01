#!/usr/bin/env bash

rm modules.order Module.symvers softirq_summary_event.ko softirq_summary_event.mod softirq_summary_event.mod.c softirq_summary_event.mod.o softirq_summary_event.o

make clean
make
