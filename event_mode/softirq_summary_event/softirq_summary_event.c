/* Module Licence - GPL
 * 
 * Software interrupt tracing module - aggregation layer
 *
 * This module attaches probes into softirq_entry and softirq_exit tracepoints
 * to collect required data
 * 
 * Data are than exposed using tracefs subsystem using tracepipe
 */

#include <linux/init.h>		// __init and __exit macros
#include <linux/module.h>	// to be able to define as module
#include <linux/tracepoint.h>	// tracepoint structs...
#include <linux/ktime.h>	// get current times
#include <linux/smp.h>		// cpu's info
#include <linux/interrupt.h>	// interrupts info
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions

#define CREATE_TRACE_POINTS
#include "softirq_summary_event.h"

#define NUM_SOFTIRQS 10
#define CPUS_TOTAL 64








/* Forward declarations */
static void softirq_summary_enter(void *data, unsigned int vec_nr);
static void softirq_summary_end(void *data, unsigned int vec_nr);

/* Tracepoint structs to work with */
static struct tracepoint *tp_softirq_enter;
static struct tracepoint *tp_softirq_exit;

static int num_cpus; // declaration for number of CPUs

/* array with names of all the interrupts */
static const char *softirq_names[NUM_SOFTIRQS] = {
	"HI",
	"TIMER",
	"NET_TX",
	"NET_RX",
	"BLOCK",
	"IRQ_POLL",
	"TASKLET",
	"SCHED",
	"HRTIMER",
	"RCU"
};

/* currently executing software interrupt - can not be same vector number
 * on the same CPU, therefore, array is safe get the correct one for timing */
static u64 current_softirq[CPUS_TOTAL][NUM_SOFTIRQS];

/* Captured info about softirqs */
struct softirq_info {
	u64 observed;		// observed times
	u64 unresolved;		// unresolved times
};

/* Capter results of the struct softirq_info stored in per/cpu and per/softirq
 * vector array */
static struct softirq_info results[CPUS_TOTAL][NUM_SOFTIRQS];









/* -----> Register and unregister module <----- */

static int __init softirq_summary_init(void)
{
	int i;
	int k;
	int ret;

	num_cpus = num_possible_cpus();

	if (CPUS_TOTAL < num_cpus) {
		pr_err("Possible number of cpus overflow!");
		return -ENOENT;
	}

	/* initialise all current_softirqs to 0 */
	for (i = 0; i < num_cpus; i++) {
		for (k = 0; k < NUM_SOFTIRQS; k++) {
			current_softirq[i][k] = 0;
			results[i][k].observed = 0;
			results[i][k].unresolved = 0;
		}
	}

	/* find and register tracepoints */
	tp_softirq_enter = find_tracepoint_by_name("softirq_entry");
	if (!tp_softirq_enter) {
		pr_err("tracepoint 'softirq_entry' not found\n");
		return -ENOENT;
	}
	
	ret = tracepoint_probe_register(tp_softirq_enter, softirq_summary_enter, NULL);
	if (ret) {
		pr_err("softirq_entry failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'softirq_entry'\n");

	tp_softirq_exit = find_tracepoint_by_name("softirq_exit");
	if (!tp_softirq_exit) {
		tracepoint_probe_unregister(tp_softirq_enter, softirq_summary_enter, NULL);
		pr_err("tracepoint 'softirq_exit' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_softirq_exit, softirq_summary_end, NULL);

	if (ret) {
		tracepoint_probe_unregister(tp_softirq_enter, softirq_summary_enter, NULL);
		pr_err("softirq_exit failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'softirq_exit'\n");

	return 0;
}

static void __exit softirq_summary_exit(void)
{
	/* if softirq_enter runs */
	if (tp_softirq_enter) {
		tracepoint_probe_unregister(tp_softirq_enter, softirq_summary_enter, NULL);
		pr_info("unregistered probe from tracepoint 'softirq_enter'\n");
		tp_softirq_enter = NULL;
	}

	/* if softirq_exit runs */
	if (tp_softirq_exit) {
		tracepoint_probe_unregister(tp_softirq_exit, softirq_summary_end, NULL);
		tp_softirq_exit = NULL;
		pr_info("unregistered probe from tracepoint 'softirq_exit'\n");
	}
	
	tracepoint_synchronize_unregister();
}








/* -----> Register and unregister probe <----- */
static void softirq_summary_enter(void *data, unsigned int vec_nr)
{
	int current_cpu;
	u64 current_time;

	(void)data;

	/* check if vec_nr is higher than me defined num_softirqs */
	if (vec_nr >= NUM_SOFTIRQS) {
		return;
	}

	current_cpu = smp_processor_id();
	current_time = ktime_get_ns();
	
	/* if something went wrong and cpu writing another softirq to 
	   the same CPU, I can increase unresolved and print softirq */
	if (current_softirq[current_cpu][vec_nr] != 0) {
		results[current_cpu][vec_nr].unresolved++;
		trace_softirq_tracer_only(
			current_cpu,
			vec_nr,
			softirq_names[vec_nr],
			current_time - current_softirq[current_cpu][vec_nr],
			results[current_cpu][vec_nr].observed,
			results[current_cpu][vec_nr].unresolved
		);
	}

	/* now, update start time - current time serves also as a flag
	   for system to recognise if there is another softirq stored.
	   Once finished, current_time is updated to 0 */
	current_softirq[current_cpu][vec_nr] = current_time;
}

static void softirq_summary_end(void *data, unsigned int vec_nr)
{
	int current_cpu;
	u64 current_time;
	
	(void)data;
	
	/* check if vec_nr is hihger than me defined num_softirqs */
	if (vec_nr >= NUM_SOFTIRQS) {
		return;
	}

	current_cpu = smp_processor_id();
	current_time = ktime_get_ns();

	/* again, if something went wrong */
	if (current_softirq[current_cpu][vec_nr] == 0) {
		results[current_cpu][vec_nr].unresolved++;
		return;
	}
	
	/* update result */
	results[current_cpu][vec_nr].observed++;
	trace_softirq_tracer_only(
		current_cpu,
		vec_nr,
		softirq_names[vec_nr],
		current_time - current_softirq[current_cpu][vec_nr],
		results[current_cpu][vec_nr].observed,
		results[current_cpu][vec_nr].unresolved
	);

	/* update current_softirq to 0 to set to the correct flat */
	current_softirq[current_cpu][vec_nr] = 0;
}








module_init(softirq_summary_init);
module_exit(softirq_summary_exit);
MODULE_LICENSE("GPL");
