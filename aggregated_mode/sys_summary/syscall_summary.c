/* Module Licence - GPL
 * 
 * Ondrej Hurinek
 * !! This module was developed as part of BSc Computer Science - Individual 3rd
 * year project !!
 * 
 * Systemcall tracing module - aggregation layer
 *
 * This module attaches probes into syscall_entry and syscall_exit tracepoints
 * to collect required data
 * 
 * Data are than exposed using /proc subsystem
 *
 * Important:	- per thread hashtable used, since 1 thread can only be
 * 		  executing 1 syscall
 */






#include <linux/init.h>		// __init and __exit macros
#include <linux/module.h>	// to be able to define as module
#include <linux/tracepoint.h>	// tracepoint structs...
#include <asm/unistd.h>		// __NR_syscalls
#include <linux/sched.h>	// thread id, proc i
#include <linux/ktime.h>	// get current times
#include <linux/hashtable.h>	// hastable
#include <linux/slab.h>		// kmalloc()
#include <asm/ptrace.h>		// structs pt_regs
#include <linux/seq_file.h>	// seq_file into proc
#include <linux/proc_fs.h>	// /proc exposure
#include <linux/spinlock.h>	// spinlock / irqsave
#include "../../common/trace_tables/trace_tables.h"	// to include defined tables
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions






/* Forward functions declarations */
static void syscall_summary_enter(void *data, struct pt_regs *regs, long id);
static void syscall_summary_end(void *data, struct pt_regs *regs, long ret);
static int print_sys(struct seq_file *f, void *v);
static int bridge_print_sys(struct inode *inode, struct file *file);
static struct tp_thread *hash_lookup(pid_t thread_id);

/* Tracepoint structs to work with */
static struct tracepoint *tp_sys_enter;
static struct tracepoint *tp_sys_exit;

/* Struct important for hashtable
   u64 -> current time at the start of the syscall */
struct tp_thread {
	pid_t thread_id;
	long sys_id;
	u64 sys_time_start;
	struct hlist_node node;
};

/* Struct to store info about captured syscalls
   observed_times -> value that increments at each syscall id fire
   total_time -> total time of all syscall id firing */
struct syscall_info {
	atomic64_t observed_times;
	atomic64_t total_time;
	atomic64_t unresolved;
};

/* Struct for proc operations */
static const struct proc_ops sys_proc_ops = {
	.proc_open 	= bridge_print_sys,
	.proc_read 	= seq_read,
	.proc_lseek 	= seq_lseek,
	.proc_release	= single_release,
};

/* Spinlock to stop particular section of the code from executing on
   the different CPUs */
static DEFINE_SPINLOCK(thread_syscall_lock);

/* Hashtable to store syscalls per thread,
   so i can measure times between enter/exit */
DEFINE_HASHTABLE(thread_syscall, 9);

/* Array to store captured syscalls and their counts
   index = syscall id */
static struct syscall_info syscall_counts[__NR_syscalls];

/* Entry for /proc_fs */
static struct proc_dir_entry *sys_proc_entry;

/* Variable to hold target_pid, -1 by default since
   tgid can never be -1, so if -1, means i m printing
   all of the system calls, othervise, target_pid loaded
   at the module loading */
static int target_pid = -1;






/* -----> sys_enter and sys_exit probes <----- */

/* Function to store thread id with current time upon entering the syscall */
static void syscall_summary_enter(void *data, struct pt_regs *regs, long id)
{
	u64 current_time;
	struct tp_thread *tp_new; 	// adding new thread
	struct tp_thread *existing;	// thread to check if it exists
	unsigned long flags;

	(void)data;
	(void)regs;

	/* check for target_pid */
	if (target_pid != -1 && current->tgid != target_pid)
		return;
	/* check for unusual syscall number */
	if (id >= __NR_syscalls)
		return;

	current_time = ktime_get_ns();

	/* allocate enough space for newly found thread */
	tp_new = kmalloc(sizeof(*tp_new), GFP_ATOMIC);
	if (!tp_new)
		return;

	tp_new->thread_id = current->pid;
	tp_new->sys_id = id;
	tp_new->sys_time_start = current_time;	

	spin_lock_irqsave(&thread_syscall_lock, flags);

	/* if threadid exist, just replace with new, and keep
	   record of unresolved syscall */
	existing = hash_lookup(current->pid);
	if (existing) {
		atomic64_inc(&syscall_counts[existing->sys_id].unresolved);
		existing->sys_id = id;
		existing->sys_time_start = current_time;
		spin_unlock_irqrestore(&thread_syscall_lock, flags);
		kfree(tp_new);
		return;
	}

	/* othervise, add thread into the hash table as new */
	hash_add(thread_syscall, &tp_new->node, tp_new->thread_id);
	
	spin_unlock_irqrestore(&thread_syscall_lock, flags);
}

static void syscall_summary_end(void *data, struct pt_regs *regs, long ret)
{
	u64 current_time;
	u64 duration;
	struct tp_thread *tp;
	unsigned long flags;

	(void)data;
	(void)regs;
	
	/* check for target_pid */
	if (target_pid != -1 && current->tgid != target_pid)
		return;

	current_time = ktime_get_ns();

	spin_lock_irqsave(&thread_syscall_lock, flags);
	
	/* search for the tracepoint to figure time */
	tp = hash_lookup(current->pid);

	if (!tp) { // if for some reason, tracepoint does not exist
		spin_unlock_irqrestore(&thread_syscall_lock, flags);
		return;
	}
	
	duration = current_time - tp->sys_time_start;

	/* remove thread from the hashtable */
	hash_del(&tp->node);

	spin_unlock_irqrestore(&thread_syscall_lock, flags);

	/* update observed_times and total time */
	atomic64_inc(&syscall_counts[tp->sys_id].observed_times);
	atomic64_add(duration, &syscall_counts[tp->sys_id].total_time);

	kfree(tp);
}






/* -----> init and exit macros <------ */

static int __init syscall_summary_init(void)
{
	int i;
	int ret;

	/* initialise syscall_counts */
	for (i = 0; i < __NR_syscalls; i++) {
		atomic64_set(&syscall_counts[i].observed_times, 0);
		atomic64_set(&syscall_counts[i].total_time, 0);
		atomic64_set(&syscall_counts[i].unresolved, 0);
	}
	
	hash_init(thread_syscall); // initialise hashtable

	/* register probes into specific tracepoints */
	tp_sys_enter = find_tracepoint_by_name("sys_enter");
	if (!tp_sys_enter) {
		pr_err("tracepoint 'sys_enter' not found\n");
		return -ENOENT;
	}
	
	ret = tracepoint_probe_register(tp_sys_enter, syscall_summary_enter, NULL);
	if (ret) {
		pr_err("sys_enter failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'sys_enter'\n");

	tp_sys_exit = find_tracepoint_by_name("sys_exit");
	if (!tp_sys_exit) {
		tracepoint_probe_unregister(tp_sys_enter, syscall_summary_enter, NULL);
		pr_err("tracepoint 'sys_exit' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_sys_exit, syscall_summary_end, NULL);
	if (ret) {
		tracepoint_probe_unregister(tp_sys_enter, syscall_summary_enter, NULL);
		pr_err("sys_exit failed: %d\n", ret);
		return ret;
	}

	pr_info("registered probe to the tracepoint 'sys_exit'\n");

	/* make array avalaible in correct format in user space */
	sys_proc_entry = proc_create("sys_trace_agg", 0444, NULL, &sys_proc_ops);
	if (!sys_proc_entry) {
		tracepoint_probe_unregister(tp_sys_enter, syscall_summary_enter, NULL);
		tracepoint_probe_unregister(tp_sys_exit, syscall_summary_end, NULL);
		return -ENOMEM;
	}

	return 0;
}

static void __exit syscall_summary_exit(void)
{	
	struct tp_thread *t;
	struct hlist_node *tmp;
	int bkt;

	if (tp_sys_enter) {	// if sys_enter runs
		tracepoint_probe_unregister(tp_sys_enter, syscall_summary_enter, NULL);
		pr_info("unregistered probe from tracepoint 'sys_enter'\n");
		tp_sys_enter = NULL;
	}

	if (tp_sys_exit) {	// if sys_exit runs
		tracepoint_probe_unregister(tp_sys_exit, syscall_summary_end, NULL);
		tp_sys_exit = NULL;
	}

	/* remove syscall proc from /proc_fs */
	if (sys_proc_entry) {
		proc_remove(sys_proc_entry);
		sys_proc_entry = NULL;
	}

	tracepoint_synchronize_unregister();
	
	/* cleanup of hashtable from the heap */
	hash_for_each_safe(thread_syscall, bkt, tmp, t, node) {
		hash_del(&t->node);
		kfree(t);
	}
}






/* ----------> Functions to expose array into /proc filesystem<---------- */

/* Function to print array once proc_open is called */
static int print_sys(struct seq_file *f, void *v)
{
	long long average_time;
	int i;
	int j = 15;
	int k;

	/* loop through array of syscalls */
	for (i = 0; i < __NR_syscalls; i++) {
		if (j == 15) {
			for (k = 0; k < 110; k++)		// print dashes
				seq_printf(f, "%s", "-");
		
			seq_printf(f, "\n%4s | %25s | %11s | %10s | %15s | %15s | %15s\n", 	// print headers
				"ID","SYSCALL NAME", "SYSCALL ABI", "OBS TIMES", "UNRESOLVED", "AVG TIME", "TOTAL TIME");

			j = 0;
		
			for (k = 0; k < 110; k++)		// print dashes
				seq_printf(f, "%s", "-");
			
			seq_printf(f, "\n");
		}

		/* if syscall were observed */
		if ((long long)atomic64_read(&syscall_counts[i].observed_times) != 0 || (long long)atomic64_read(&syscall_counts[i].unresolved) > 0) {

			if ((long long)atomic64_read(&syscall_counts[i].observed_times) == 0) {
				average_time = 0;
			} else {
				/* calculate averate time of the processing the syscall */
				average_time = (long long)atomic64_read(&syscall_counts[i].total_time) / 
					(long long)atomic64_read(&syscall_counts[i].observed_times);
			}

			/* print results ---- !!need to add name of the syscall!!*/
			seq_printf(f, "%4d | %25s | %11s | %10lld | %15lld | %15lld | %15lld\n", 
				i, get_syscall_name(i), get_syscall_abi(i), 
				(long long)atomic64_read(&syscall_counts[i].observed_times), 
				(long long)atomic64_read(&syscall_counts[i].unresolved),
				average_time, (long long)atomic64_read(&syscall_counts[i].total_time)); j++;
		}
	}

	return 0;
}

/* Function to bridge print_sys with the function that creates proc file
   !!must be this signature!! */
static int bridge_print_sys(struct inode *inode, struct file *file)
{
	return single_open(file, print_sys, NULL);
}






/* -----> other <----- */

/*Hashtable lookup function*/
static struct tp_thread *hash_lookup(pid_t thread_id)
{
	struct tp_thread *t;
	
	hash_for_each_possible(thread_syscall, t, node, thread_id) {
		if (t->thread_id == thread_id)
			return t;
	}

	return NULL;
}






module_init(syscall_summary_init);	// to hook init into module loader
module_exit(syscall_summary_exit);	// to hook exit into module loader
/* to init target_pid with module parameter, 
type int and 0644 = file permission*/
module_param(target_pid, int, 0644);
MODULE_LICENSE("GPL");			// necessary to have licence
