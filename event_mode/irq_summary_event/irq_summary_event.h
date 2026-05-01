/* This file contains custom trace event definition to expose captured data into the userspace
 * using ftrace -> tracing buffer
 *
 * using trace and trace_pipe, it will appear as:
 * 	irq_tracer:irq_tracer_only
*/









/* Define trace subsystem name (appears in trace output)*/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq_tracer

/* Header guard that is required for tracepoint headers*/
#if !defined(_TRACE_IRQ_TRACER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_TRACER_H

#include <linux/tracepoint.h>










/* TRACE_EVENT	-> defines a tracepoint called irq_traer_only
 * TP_PROTO	-> function prototype -> basically just a signature on how i call trace_irq_tracer_only form module
 * TP_ARGS	-> arguments passed internally */
TRACE_EVENT(irq_tracer_only,
	TP_PROTO(int irq_id, const char *device_name, u64 exe_time, u64 total_time, u64 observed_times),
	TP_ARGS(irq_id, device_name, exe_time, total_time, observed_times),

	/* Structure stored inside the ringbuffer */
	TP_STRUCT__entry(
		__field(int, irq_id)			// irq number
		__string(device_name, device_name)	// device name
		__field(u64, exe_time)			// execution time of irq
		__field(u64, total_time)		// total time of executing this irqs
		__field(u64, observed_times)		// count of this irqs
	),

	/* Assign values from arguments into the struct */
	TP_fast_assign(
		__entry->irq_id = irq_id;
		__assign_str(device_name, device_name);
		__entry->exe_time = exe_time;
		__entry->total_time = total_time;
		__entry->observed_times = observed_times;
	),

	/* Formated output into tracebuffer */
	TP_printk("irq id=%d device name=%s duration=%llu total time=%llu count=%llu",
		__entry->irq_id,
		__get_str(device_name),
		__entry->exe_time,
		__entry->total_time,
		__entry->observed_times
	)
);










#endif

/* required: must be outside the header guard
 * tells the header generator where the header lives */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

/* tells it the base name without .h */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE irq_summary_event

/* triggers the macro expansion that generates the actuall trace event code */
#include <trace/define_trace.h>
