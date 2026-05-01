/* This file contains custom trace event definition to expose captured data into the userspace
 * using ftrace -> tracing buffer
 *
 * using trace and trace_pipe, it will appear as:
 * 	softirq_tracer:softirq_tracer_only
*/








/* Define trace subsystem name (appears in trace output)*/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM softirq_tracer

/* Header guard that is required for tracepoint headers*/
#if !defined(_TRACE_SOFTIRQ_TRACER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SOFTIRQ_TRACER_H

#include <linux/tracepoint.h>









/* TRACE_EVENT	-> defines a tracepoint called softirq_traer_only
 * TP_PROTO	-> function prototype -> basically just a signature on how i call trace_softirq_tracer_only form module
 * TP_ARGS	-> arguments passed internally */
TRACE_EVENT(softirq_tracer_only,
	TP_PROTO(int current_cpu, unsigned int vec_nr, const char *softirq_name, u64 exec_softirq, u64 observed, u64 unresolved),
	TP_ARGS(current_cpu, vec_nr, softirq_name, exec_softirq, observed, unresolved),

	/* Structure stored inside the ringbuffer */
	TP_STRUCT__entry(
		__field(int, current_cpu)
		__field(unsigned int, vec_nr)
		__string(softirq_name, softirq_name)
		__field(u64, exec_softirq)
		__field(u64, observed)
		__field(u64, unresolved)
	),

	/* Assign values from arguments into the struct */
	TP_fast_assign(
		__entry->current_cpu = current_cpu;
		__entry->vec_nr = vec_nr;
		__assign_str(softirq_name, softirq_name);
		__entry->exec_softirq = exec_softirq;
		__entry->observed = observed;
		__entry->unresolved = unresolved;
	),

	/* Formated output into tracebuffer */
	TP_printk("CPU=%d SOFTIRQNR=%u softirq name=%s duration=%llu observed=%llu unresolved=%llu",
		__entry->current_cpu,
		__entry->vec_nr,
		__get_str(softirq_name),
		__entry->exec_softirq,
		__entry->observed,
		__entry->unresolved
	)
);








#endif

/* required: must be outside the header guard
 * tells the header generator where the header lives */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

/* tells it the base name without .h */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE softirq_summary_event

/* triggers the macro expansion that generates the actuall trace event code */
#include <trace/define_trace.h>
