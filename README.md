# linux_kernel_tracer
Lightweight Kernel-Level Event Tracer and Profiler for Linux
Module Licence - GPL

Developed by: Ondrej Hurinek

This module was developed as part of BSc Computer Science - individual 3rd year project

!Important -> instruction pointer resolution requires fix, does not work!


Flags:
	-m -> to define module name for laoding
	
	-p -> to define process id where applicable (if not applicable, it is ignored)
	
	-s -> to define syscall id to trace where applicable (if not applicable, it is ignored)
	
	-o -> to define output file to save output into
	
	-n -> to define number of output values for ip_summary
	
	-f -> to define proc_file - file to read /proc subsystem, set correctly by default but,
	      if there is requirement to read different /proc file, use this flag to define where 
	      exactly to read it from (for example /proc/interrupts

Example of usage: ./tracer.out -m ip_summary -p 5028 -s 0 -o output_file.txt -n 20 -f /proc/interrupts

Notes: 	- To print data in Aggregated mode, press 'p'
	- To quit, press q or Ctrl+c

Modules avalaible to load:
	Aggregated mode: - ip_summary -> to print hottest instruction pointers
					 Avalaible flags: -p -s -n -o
			 - irq_summary -> to print summary of traced interrupts
					  Avalaible flags: -o
			 - softirq_summary -> to print summary of traced software interrupts
					      Avalaible flags: -o	
			 - sys_summary -> to print summary of traced system calls
	Event mode: - irq_event -> to trace events of runtime interrupts
				   Avalaible flags: -o
		    - softirq_event -> to trace events of runtime software interrupts
				       Avalaible flags: -o
		    - syscall_event -> to trace events of runtime system calls
				       Avalaible flags: -p -s -o

Important:	- the tracer uses the linux tracefs interface (/sys/kernel/tracing/trace_pipe)
		  to read emitted events in event mode. It is important to note, that tracefs
		  is a global subsystem, and any changes to its configuration, and / or writing
		  to it may resolve in incorrect data. For this reason, it is recommende to
		  manually clear the tracing subsystem. This can be done by disabling tracing,
		  clearing the trace buffer and than reenabling tracing again.
		  Disable tracing:
			echo 0 | sudo tee /sys/kernel/tracing/tracing_on
		  Disable currently running events:
			echo 0 | sudo tee /sys/kernel/tracing/events/enable
		  Clear the trace buffer:
			echo | sudo tee /sys/kernel/tracing/trace
		  Reenable tracing:
			echo 1 | sudo tee /sys/kernel/tracing/tracing_on
