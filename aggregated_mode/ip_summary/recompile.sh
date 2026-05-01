#!/usr/bin/env bash

rm modules.order Module.symvers ip_summary.ko ip_summary.mod ip_summary.mod.c ip_summary.mod.o ip_summary.o ../../common/trace_common/tp_lookup.o

make clean
make
