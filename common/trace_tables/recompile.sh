#!/usr/bin/env bash

rm modules.order Module.symvers trace_tables.ko trace_tables.mod trace_tables.mod.c trace_tables.mod.o trace_tables.o

make clean
make
