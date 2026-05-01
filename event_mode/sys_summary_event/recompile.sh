#!/usr/bin/env bash

rm modules.order Module.symvers sys_summary_event.ko sys_summary_event.mod sys_summary_event.mod.c sys_summary_event.mod.o sys_summary_event.o

make clean
make
