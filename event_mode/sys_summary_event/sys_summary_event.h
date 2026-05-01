/* This file contains custom trace event definition to expose captured data into the userspace
 * using ftrace -> tracing buffer
 *
 * using trace and trace_pipe, it will appear as:
 * 	sys_tracer:sys_tracer_only
*/









/* Define trace subsystem name (appears in trace output)*/
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sys_tracer

/* Header guard that is required for tracepoint headers*/
#if !defined(_TRACE_SYS_TRACER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SYS_TRACER_H

#include <linux/tracepoint.h>









/* TRACE_EVENT	-> defines a tracepoint called sys_tracer_only
 * TP_PROTO	-> function prototype -> basically just a signature on how i call trace_sys_tracer_only form module
 * TP_ARGS	-> arguments passed internally */
TRACE_EVENT(sys_tracer_only,
	TP_PROTO(long id, const char *sys_name, u64 duration, u64 count, bool resolved, u64 unresolved_count, int proc_id, const char *proc_name, unsigned long ip, const char *image_name),
	TP_ARGS(id, sys_name, duration, count, resolved, unresolved_count, proc_id, proc_name, ip, image_name),

	/* Structure stored inside the ringbuffer */
	TP_STRUCT__entry(
		__field(long, id)		// syscall id
		__string(sys_name, sys_name)	// syscall name
		__field(u64, duration)		// syscall execution duration
		__field(u64, count)		// syscall count
		__field(bool, resolved)		// syscall resolved boolean
		__field(u64, unresolved_count)	// syscall unresolved count
		__field(int, proc_id)		// syscall process id
		__string(proc_name, proc_name)	// syscall process name
		__field(unsigned long, ip)	// syscall instruction pointer
		__string(image_name, image_name)// ip image name
	),

	/* Assign values from arguments into the struct */
	TP_fast_assign(
		__entry->id = id;
		__assign_str(sys_name, sys_name);
		__entry->duration = duration;
		__entry->count = count;
		__entry->resolved = resolved;
		__entry->unresolved_count = unresolved_count;
		__entry->proc_id = proc_id;
		__assign_str(proc_name, proc_name);
		__entry->ip = ip;
		__assign_str(image_name, image_name);
	),

	/* Formated output into tracebuffer */
	TP_printk("id=%ld name=%s duration=%llu count=%llu resolved=%s unresolved=%llu process id=%d proc name=%s proc ip=0x%lx image name=%s",
		__entry->id,
		__get_str(sys_name),
		__entry->duration,
		__entry->count,
		__entry->resolved == 0 ? "false" : "true",
		__entry->unresolved_count,
		__entry->proc_id,
		__get_str(proc_name),
		__entry->ip,
		__get_str(image_name)
	)
);









#endif

/* required: must be outside the header guard
 * tells the header generator where the header lives */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

/* tells it the base name without .h */
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE sys_summary_event

/* triggers the macro expansion that generates the actuall trace event code */
#include <trace/define_trace.h>
