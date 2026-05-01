#!/usr/bin/env bash

rm modules.order Module.symvers syscall_summary.ko syscall_summary.mod syscall_summary.mod.c syscall_summary.mod.o syscall_summary.o ../../common/trace_common/tp_lookup.o

make clean
make
