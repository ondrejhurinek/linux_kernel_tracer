/* Module Licence - GPL
 * 
 * Ondrej Hurinek
 * !! This module was developed as part of BSc Computer Science - Individual 3rd
 * year project !!
 * 
 * Interrupt tracing module - aggregation layer
 *
 * This module attaches probes into irq_handler_entry and irq_handler_exit tracepoints
 * to collect required data
 * 
 * Data are than exposed using /proc subsystem
 *
 * Important:	- per CPU data structures are important for cross CPU irq validation
 * 		- interrupt nesting is handled using stack-like array
 */









#include <linux/string.h>	// strcmp
#include <linux/init.h>		// __init and __exit macros
#include <linux/module.h>	// to be able to define as module
#include <linux/tracepoint.h>	// tracepoint structs...
#include <linux/ktime.h>	// get current times
#include <linux/spinlock.h>	// spinlock / irqsave
#include <linux/smp.h>		// cpu's info
#include <linux/interrupt.h>	// interrupts info
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions

#define CREATE_TRACE_POINTS
#include "irq_summary_event.h"

#define MAX_DEPTH 16		// Max depth of the nested interrupts
#define IRQ_NUMBER 512		// Upper bound of an array to store irq numbers
#define IRQ_PER_NUMB 16		// Interrupts per irq number (can be more than 1)
#define CPUS_TOTAL 64		// Number of cpu's executing







/* forward declarations */
static void irq_summary_enter(void *data, int irq, struct irqaction *action);
static void irq_summary_end(void *data, int irq, int ret);

/* spinlock to be used to avoid nested interrupts modifying data incorrectly */
static DEFINE_SPINLOCK(concurrency_irq_lock);

/* Tracepoint structs to work with */
static struct tracepoint *tp_irq_enter;
static struct tracepoint *tp_irq_exit;

/* Struct to store all the informations catched from the interrupt */
struct irq_info {
	int irq_id;
	char device_name[128];
	u64 starting_time;
	u64 total_time;
	u64 observed_times;
	void *dev_id;
};

/* Struct that holds data for each particular cpu */
struct per_cpu_stack {
	u64 top;				// top of the stack
	struct irq_info cpu_stack[MAX_DEPTH];	// stack array
};

/* Array to hold data of all cpus */
static struct per_cpu_stack cpus[CPUS_TOTAL];

/* Array to store the results of irq info / been per/cpu is safer!! */
static struct irq_info irq_results[CPUS_TOTAL][IRQ_NUMBER][IRQ_PER_NUMB];








/* -----> module registration and unregistration macros <----- */

static int __init irq_summary_init(void)
{
	int i;
	int j;
	int k;
	int ret;
	int num_cpus = num_possible_cpus();

	if (num_cpus > CPUS_TOTAL) {
		pr_err("not enough CPUS_TOTAL defined");
		return -ENOENT;
	}
	
	/* Initialise all top of the stack to 0 */
	for (i = 0; i < num_cpus; i++) {
		cpus[i].top = 0;
	}

	/* Initialise all dev_id's pointers to NULL */
	for (j = 0; j < num_cpus; j++) {
		for (i = 0; i < IRQ_NUMBER; i++) {
			for (k = 0; k < IRQ_PER_NUMB; k++) {
				irq_results[j][i][k].dev_id = NULL;
				irq_results[j][i][k].observed_times = 0;
			}
		}
	}
	
	tp_irq_enter = find_tracepoint_by_name("irq_handler_entry");
	
	if (!tp_irq_enter) {
		pr_err("tracepoint 'irq_handler_entry' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_irq_enter, irq_summary_enter, NULL);
	if (ret) {
		pr_err("irq_handler_entry failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'irq_handler_entry'\n");

	tp_irq_exit = find_tracepoint_by_name("irq_handler_exit");
	if (!tp_irq_exit) {
		tracepoint_probe_unregister(tp_irq_enter, irq_summary_enter, NULL);
		pr_err("tracepoint 'irq_handler_exit' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_irq_exit, irq_summary_end, NULL);

	if (ret) {
		tracepoint_probe_unregister(tp_irq_enter, irq_summary_enter, NULL);
		pr_err("irq_exit failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'irq_handler_exit'\n");

	return 0;
}

static void __exit irq_summary_exit(void)
{
	/* if irq_enter runs */
	if (tp_irq_enter) {
		tracepoint_probe_unregister(tp_irq_enter, irq_summary_enter, NULL);
		pr_info("unregistered probe from tracepoint 'irq_handler_enter'\n");
		tp_irq_enter = NULL;
	}

	// if irq_exit runs
	if (tp_irq_exit) {
		tracepoint_probe_unregister(tp_irq_exit, irq_summary_end, NULL);
		pr_info("unregistered probe from tracepoint 'irq_handler_exit'\n");
		tp_irq_exit = NULL;
	}

	tracepoint_synchronize_unregister();
}








/* -----> probe functions <----- */

static void irq_summary_enter(void *data, int irq, struct irqaction *action)
{
	int current_cpu;
	(void)data;

	struct irq_info temp = {
		.irq_id = irq,
		.starting_time = ktime_get_ns(),
		.dev_id = action->dev_id,
	};

	current_cpu = smp_processor_id();

	/* check for IRQ_NUMBER overflow */
	if (irq >= IRQ_NUMBER) {
		pr_err("overflow in constant IRQ_NUMBER");
		return;
	}

	/* now, check for max_depth overflow */
	if (cpus[current_cpu].top >= MAX_DEPTH) {
		pr_err("overflow in variable MAX_DEPTH");
		return;
	}

	if (action->name == NULL) {
		strscpy(temp.device_name, "undefined", sizeof(temp.device_name));
	} else {
		strscpy(temp.device_name, action->name, sizeof(temp.device_name));
	}

	/* if this interrupt have interrupted 1 before at the same cpu */
	if (cpus[current_cpu].top > 0) {
		cpus[current_cpu].cpu_stack[cpus[current_cpu].top - 1].total_time += ktime_get_ns() - cpus[current_cpu].cpu_stack[cpus[current_cpu].top - 1].starting_time;
	}

	/* increase top of the stack in case this interrupt gets interrupted */
	cpus[current_cpu].top++;
	
	/* alocate temp into cpus array */
	cpus[current_cpu].cpu_stack[cpus[current_cpu].top - 1] = temp;
}

static void irq_summary_end(void *data, int irq, int ret)
{
	int current_cpu;
	u64 current_time;
	int i;
	struct irq_info finished;
	bool updated;
	unsigned long flags;

	(void)data;
	(void)ret;

	struct irq_info *result = NULL;

	current_time = ktime_get_ns();
	current_cpu = smp_processor_id();
	updated = false;
	
	/* check for IRQ_NUMBER overflow */
	if (irq >= IRQ_NUMBER) {
		pr_err("overflow in constant IRQ_NUMBER");
		return;
	}
	
	/* check for max_depth overflow or underflow */
	if (cpus[current_cpu].top >= MAX_DEPTH || cpus[current_cpu].top < 1) {
		pr_err("overflow or underflow in variable MAX_DEPTH");
		return;
	}
	
	finished = cpus[current_cpu].cpu_stack[cpus[current_cpu].top - 1];
	/* in case there was a mismatch looking for previous nested irq */
	if (finished.irq_id != irq) {
		pr_err("irq mismatch on cpu %d: exit irq=%d, stack irq=%d\n",
		       current_cpu, irq, finished.irq_id);
		cpus[current_cpu].top--;
		return;
	}

	finished.total_time += current_time - finished.starting_time;	

	spin_lock_irqsave(&concurrency_irq_lock, flags); // lock modifying results here

	/* loop through all interrupts inside interrupt number */
	for (i = 0; i < IRQ_PER_NUMB; i++) {
		if (irq_results[current_cpu][irq][i].dev_id == finished.dev_id) {
			/* update existing irq_result */
			irq_results[current_cpu][irq][i].observed_times++;
			/* irq_results[current_cpu][irq][i].total_time += current_time - irq_results[current_cpu][irq][i].starting_time; */
			irq_results[current_cpu][irq][i].total_time += finished.total_time;
			result = &irq_results[current_cpu][irq][i];
			updated = true;
			break;
		} else if (irq_results[current_cpu][irq][i].dev_id == NULL) { // if result slot empty
			/* asign here from stack */
			irq_results[current_cpu][irq][i] = finished;
			irq_results[current_cpu][irq][i].observed_times = 1;
			result = &irq_results[current_cpu][irq][i];
			updated = true;
			break;
		}
	}

	spin_unlock_irqrestore(&concurrency_irq_lock, flags); // unlock spinlock here

	/* if there was an interrupted interrupt that needs to be updated */
	if (cpus[current_cpu].top > 1) {
		/* update starting time of the previou interrupt */
		cpus[current_cpu].cpu_stack[cpus[current_cpu].top - 2].starting_time = current_time;
	}

	/* decrese top of the stack since all have been assigned */
	cpus[current_cpu].top--;
	
	/* check if data were updated (to avoid nested number overflow) */
	if (!updated) {
		pr_err("IRQ_PER_NUMB too small");
	} else {
		/* expose data into trace buffer to be printed */
		trace_irq_tracer_only(
			irq,		 				// irq number
			result->device_name, 				// device name
			current_time - finished.starting_time, 		// time spend executin interrupt
			result->total_time,				// total time spent on all irqs
			result->observed_times 				// count
		);
	}

}







module_init(irq_summary_init);
module_exit(irq_summary_exit);
MODULE_LICENSE("GPL");
