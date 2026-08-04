/* Module Licence - GPL
 * 
 * Software interrupt tracing module - aggregation layer
 *
 * This module attaches probes into softirq_entry and softirq_exit tracepoints
 * to collect required data
 * 
 * Data are than exposed using /proc subsystem
 *
 * Important:	- per CPU data structures are important, since there is a g
 * 		  guarantee having only 1 same softirq running on same CPU
 */

#include <linux/string.h>	// strcmp
#include <linux/init.h>		// __init and __exit macros
#include <linux/module.h>	// to be able to define as module
#include <linux/tracepoint.h>	// tracepoint structs...
#include <linux/ktime.h>	// get current times
#include <linux/seq_file.h>	// seq_file into proc
#include <linux/proc_fs.h>	// /proc
#include <linux/smp.h>		// cpu's info
#include <linux/interrupt.h>	// for interrupts info
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions

#define NUM_SOFTIRQS 10		// maximum number of suftirq (for array init)
#define CPUS_TOTAL 64		// CPUs variable for stack array init






/* Array with names of all the interrupts */
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

/* Forward declarations */
static int bridge_print_softirq(struct inode *inode, struct file *file);
static int print_softirq(struct seq_file *f, void *v);
static void softirq_summary_enter(void *data, unsigned int vec_nr);
static void softirq_summary_end(void *data, unsigned int vec_nr);

/* Tracepoint structs to work with */
static struct tracepoint *tp_softirq_enter;
static struct tracepoint *tp_softirq_exit;

static int num_cpus;		// real number of CPUs (init on loadup)

/* Entry for /proc_fs */
static struct proc_dir_entry *softirq_proc_entry;

/* Softirq currently running on CPU (u64 as softirq vector), othervise 0 */
static u64 current_softirq[CPUS_TOTAL][NUM_SOFTIRQS];

/* Results struct */
struct softirq_info {
	char name[10];
	u64 observed;
	u64 total_time;
	u64 unresolved;
};

/* Struct for proc operations */
static const struct proc_ops softirq_proc_ops = {
	.proc_open 	= bridge_print_softirq,
	.proc_read 	= seq_read,
	.proc_lseek 	= seq_lseek,
	.proc_release	= single_release,
};

/* Result array */
static struct softirq_info results[CPUS_TOTAL][NUM_SOFTIRQS];






/* -----> Register and unregister module <----- */
static int __init softirq_summary_init(void)
{
	int i;
	int k;
	int ret;

	num_cpus = num_possible_cpus();

	if (CPUS_TOTAL < num_cpus) {
		pr_err("Possible number of cpus overflow!\n");
		return -ENOENT;
	}

	/* initialise all current_softirqs to 0 */
	for (i = 0; i < num_cpus; i++) {
		for (k = 0; k < NUM_SOFTIRQS; k++) {
			current_softirq[i][k] = 0;
		}
	}

	/* Probe registrations into the tracepoints */

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
	
	pr_info("registered probe to the tracepoint 'softirq_handler_entry'\n");

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

	/* make array avalaible in correct format in user space using /proc */
	softirq_proc_entry = proc_create("softirq_trace_agg", 0444, NULL, &softirq_proc_ops);
	if (!softirq_proc_entry) {
		tracepoint_probe_unregister(tp_softirq_enter, softirq_summary_enter, NULL);
		tracepoint_probe_unregister(tp_softirq_exit, softirq_summary_end, NULL);
		return -ENOMEM;
	}
	
	return 0;
}

static void __exit softirq_summary_exit(void)
{
	if (tp_softirq_enter) { 		// if softirq_enter runs
		tracepoint_probe_unregister(tp_softirq_enter, softirq_summary_enter, NULL);
		pr_info("unregistered probe from tracepoint 'softirq_enter'\n");
		tp_softirq_enter = NULL;
	}

	if (tp_softirq_exit) {			// if softirq_exit runs
		tracepoint_probe_unregister(tp_softirq_exit, softirq_summary_end, NULL);
		pr_info("unregistered probe from tracepoint 'softirq_exit'\n");
		tp_softirq_exit = NULL;
	}
	
	/* Synchronise if there are any processes still running using tracepoints */
	tracepoint_synchronize_unregister();

	/* remove softirq call proc from /proc_fs */
	if (softirq_proc_entry) {
		proc_remove(softirq_proc_entry);
		softirq_proc_entry = NULL;
	}
}






/* -----> Register and unregister probe <----- */

static void softirq_summary_enter(void *data, unsigned int vec_nr)
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
	
	/* if something went wrong and cpu writing another softirq to the same cpu */
	if (current_softirq[current_cpu][vec_nr] != 0) {
		results[current_cpu][vec_nr].unresolved++;
	}

	/* now, update start time */
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
	strscpy(results[current_cpu][vec_nr].name, softirq_names[vec_nr], sizeof(results[current_cpu][vec_nr].name));
	results[current_cpu][vec_nr].observed++;
	results[current_cpu][vec_nr].total_time += current_time - current_softirq[current_cpu][vec_nr];
	
	/* update current_softirq to 0 */
	current_softirq[current_cpu][vec_nr] = 0;
}






/* -----> proc_ops functions <----- */

/* Function to bridge print_softirq with the function that creates proc file
   !!must be this signature!! */
static int bridge_print_softirq(struct inode *inode, struct file *file)
{
	return single_open(file, print_softirq, NULL);
}

/* Function to print results using proc_ops 
 * Signature necessary for /proc exposure */
static int print_softirq(struct seq_file *f, void *v)
{
	long long average_time;
	int i;
	int j;
	int k;
	int l;

	l = 15;

	for (i = 0; i < num_cpus; i++) {
		for (j = 0; j < NUM_SOFTIRQS; j++) {
			if (l == 15) {
				for (k = 0; k < 110; k++)	// print dashes
					seq_printf(f, "-");

				seq_printf(f, "\n%10s | %25s | %10s | %15s | %15s | %15s\n", 	// print headers
					"PROC NO","SOFTIRQ NAME", "OBS TIMES", "UNRESOLVED", "AVG TIME", "TOTAL TIME");

				for (k = 0; k < 110; k++)	// print dashes
					seq_printf(f, "-");

				seq_printf(f, "\n");
	
				l = 0;
			}
			
			if (results[i][j].observed != 0 || results[i][j].unresolved != 0) {
				/* to avoid division by 0 */
				if (results[i][j].observed == 0) {
					average_time = 0;
				} else {
					average_time = results[i][j].total_time / results[i][j].observed;
				}
				
				seq_printf(f, "%10d | %25s | %10llu | %15llu | %15lld | %15llu\n", 	// print header
					i,
					results[i][j].name,
					results[i][j].observed,
					results[i][j].unresolved,
					average_time,
					results[i][j].total_time);

				l++;
			}
		}
	}

	return 0;
}






module_init(softirq_summary_init);
module_exit(softirq_summary_exit);
MODULE_LICENSE("GPL");
