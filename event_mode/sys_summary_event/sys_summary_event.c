/* Module Licence - GPL
 * 
 * System call module - emission layer
 *
 * This module attaches probes into sys_trace_entry and sys_trace_exit tracepoints
 * to collect required data
 * 
 * Data are than exposed using tracefs subsystem using tracepipe
 *
 * Important:	- per CPU data structures are important for cross CPU irq validation
 * 		- interrupt nesting is handled using stack-like array
 */

#include <linux/module.h> 	// to give me module licence function
#include <linux/init.h> 	// init/exit macros
#include <linux/string.h> 	// strcmp
#include <linux/tracepoint.h>	// tracepint structs....
#include <linux/ktime.h>	// get current time
#include <linux/spinlock.h>	// get spinlock
#include <linux/sched.h>	// thread id, proc id
#include <linux/slab.h>		// kmalloc
#include <linux/hashtable.h>	// hashtable
#include <linux/ptrace.h> 	// for pt_regs and instruction_pointer(regs)
#include <linux/mm.h> 		// for list of mapped file names by memory addresses (to find my own), also to process
// data like mm_struct, vm_area_struct and find_vma()
#include <linux/fs.h> 		// to access vma->vm_file which is struct file *
#include <linux/dcache.h> 	// to get the name of the ip's base
#include "../../common/trace_tables/trace_tables.h"	// to include defined tables
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions

/* To make sure that, i define tracepoint here that is defined in .h file */
#define CREATE_TRACE_POINTS
#include "sys_summary_event.h"









/* Forward function declarations */
static struct tp_thread *hash_lookup(pid_t thread_id);
static void resolve_user_ip_image(struct task_struct *task, unsigned long ip, char *buf, size_t buflen);
static void sys_trace_enter(void *data, struct pt_regs *regs, long id);
static void sys_trace_end(void *data, struct pt_regs *regs, long ret);

/* Tracepoint structs to work with */
static struct tracepoint *tp_sys_enter;
static struct tracepoint *tp_sys_exit;

/* Struct important for hashtable
   u64 -> current time at the start of the syscall */
struct tp_thread {
	pid_t thread_id;
	long sys_id;
	u64 sys_time_start;
	unsigned long inst_point;
	char proc_name[TASK_COMM_LEN];
	char image_name[64];
	struct hlist_node node;
};

/* Struct to store info about captured syscalls
   observed_times -> value that increments at each syscall id fire
   total_time -> total time of all syscall id firing */
struct sys_info {
	atomic64_t count;
	atomic64_t total_time;
	atomic64_t unresolved_count;
};

/* Spinlock definition */
static DEFINE_SPINLOCK(thread_syscall_lock);

/* Hashtable to store syscalls per thread,
   so i can measure times between enter/exit */
DEFINE_HASHTABLE(thread_syscall, 9);

/* Variable to hold target_pid, -1 by default since
   tgid can never be -1, so if -1, means i m printing
   all of the system calls, othervise, target_pid loaded
   at the module loading */
static int target_pid = -1;
/* variable to hold syscall traget id */
static int target_syscall = -1;

/* Array to store captured syscalls and their count index = syscall id */
static struct sys_info syscall_counts[__NR_syscalls];









/* -----> init and exit macros for probe registrations into tracepoints <----- */

/* -looks up trace point sys_enter to find it, then store it
   -than registers my callback with tracepoint_probe_reg.... */
static int __init sys_trace_init(void)
{
    	int ret;
	int i;

	/* init thread hashtable and syscall_counts[__NR_syscalls] */
	hash_init(thread_syscall);

	for(i = 0; i < __NR_syscalls; i++) {
		atomic64_set(&syscall_counts[i].count, 0);
		atomic64_set(&syscall_counts[i].total_time, 0);
		atomic64_set(&syscall_counts[i].unresolved_count, 0);
	}

	tp_sys_enter = find_tracepoint_by_name("sys_enter");
	if (!tp_sys_enter)
		return -ENOENT;

	ret = tracepoint_probe_register(tp_sys_enter, sys_trace_enter, NULL);
	if (ret) {
		pr_err("sys_enter failed: %d\n", ret);
		return ret;
	}

	tp_sys_exit = find_tracepoint_by_name("sys_exit");
	if (!tp_sys_exit) {
		tracepoint_probe_unregister(tp_sys_enter, sys_trace_enter, NULL);
		pr_err("tracepoint 'sys_exit' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_sys_exit, sys_trace_end, NULL);
	if (ret) {
		tracepoint_probe_unregister(tp_sys_enter, sys_trace_enter, NULL);
		pr_err("sys_exit failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit sys_trace_exit(void)
{
	struct tp_thread *t;
	struct hlist_node *tmp;
	int bkt;

	/* if sys_enter runs */
	if (tp_sys_enter) {
		tracepoint_probe_unregister(tp_sys_enter, sys_trace_enter, NULL);
		pr_info("unregistered probe from tracepoint 'sys_enter'\n");
		tp_sys_enter = NULL;
	}

	/* if sys_exit runs */
	if (tp_sys_exit) {
		tracepoint_probe_unregister(tp_sys_exit, sys_trace_end, NULL);
		pr_info("unregistered probe from tracepoint 'sys_exit'\n");
		tp_sys_exit = NULL;
	}

	tracepoint_synchronize_unregister();
	
	/* cleanup of hashtable from the heap */
	hash_for_each_safe(thread_syscall, bkt, tmp, t, node) {
		hash_del(&t->node);
		kfree(t);
	}
}










/* -----> probe registration functions <----- */

static void sys_trace_enter(void *data, struct pt_regs *regs, long id)
{
	u64 current_time;
	struct tp_thread *tp_new; 	// adding new thread
	struct tp_thread *existing;	// thread to check if it exists
	unsigned long flags;
	char sys_name[25];
	unsigned long ip;
	char image_name[64];

	char proc_name[TASK_COMM_LEN];
	unsigned long emit_ip;
	char emit_proc_name[TASK_COMM_LEN];
	char emit_image_name[64];
	u64 emit_unresolved_count;

	(void)data;

	/* check if i track 1 syscall or all */
	if (target_syscall != -1 && id != target_syscall)
		return;	

	/* check for target_pid */
	if (target_pid != -1 && current->tgid != target_pid)
		return;

	ip = instruction_pointer(regs);
	strscpy(proc_name, current->comm, sizeof(proc_name));

	/* here, i will check if i m tracking only 1 syscall, if so, 
	   i will resolve user ip image for accuracy, if not,
	   i will mark it as illegal, since overhead would couse it 
	   to crash the system */
	if (target_syscall != -1 && target_pid != -1) {
		resolve_user_ip_image(current, ip, image_name, sizeof(image_name));
	} else {
		strcpy(image_name, "undefined");
	}

	/* If syscall id out of range */
	if (id < 0 || id >= __NR_syscalls)
		return;

	current_time = ktime_get_ns();

	tp_new = kmalloc(sizeof(*tp_new), GFP_ATOMIC);

	if (!tp_new)
		return;

	tp_new->inst_point = ip;
	strscpy(tp_new->proc_name, proc_name, sizeof(tp_new->proc_name));
	strscpy(tp_new->image_name, image_name, sizeof(tp_new->image_name));
	tp_new->thread_id = current->pid;
	tp_new->sys_id = id;
	tp_new->sys_time_start = current_time;	

	spin_lock_irqsave(&thread_syscall_lock, flags);

	/* if threadid exist, just replace with new, and keep
	   record of unresolved syscall */
	existing = hash_lookup(current->pid);
	if (existing) {
		long old_id = existing->sys_id;
		existing->sys_id = id;
		atomic64_inc(&syscall_counts[old_id].unresolved_count);
		existing->sys_time_start = current_time;
		strscpy(sys_name, get_syscall_name(old_id), sizeof(sys_name));
		existing->inst_point = ip;
		strscpy(existing->proc_name, proc_name, sizeof(existing->proc_name));
		strscpy(existing->image_name, image_name, sizeof(existing->image_name));
		
		/* copy all into local variables (in case memory addresses change) */
		emit_ip = existing->inst_point;
		strscpy(emit_proc_name, existing->proc_name, sizeof(emit_proc_name));
		strscpy(emit_image_name, existing->image_name, sizeof(emit_image_name));
		emit_unresolved_count = (u64)atomic64_read(&syscall_counts[old_id].unresolved_count);

		spin_unlock_irqrestore(&thread_syscall_lock, flags);
		/* to write info into trace_pipe */
    		trace_sys_tracer_only(
			old_id, 
			get_syscall_name(old_id), 
			0, 
			(long long)atomic64_read(&syscall_counts[old_id].count), 
			false, 
			(long long)atomic64_read(&syscall_counts[old_id].unresolved_count),
			current->tgid,
			emit_proc_name,
			emit_ip,
			emit_image_name
		);
		kfree(tp_new);
		return;
	}

	hash_add(thread_syscall, &tp_new->node, tp_new->thread_id);
	spin_unlock_irqrestore(&thread_syscall_lock, flags);
}

static void sys_trace_end(void *data, struct pt_regs *regs, long ret)
{
	u64 current_time;
	u64 duration;
	struct tp_thread *tp;
	unsigned long flags;
	char sys_name[25];
	char proc_name[TASK_COMM_LEN];

	(void)data;
	(void)regs;
	(void)ret;

	/* check for target_pid */
	if (target_pid != -1 && current->tgid != target_pid)
		return;

	current_time = ktime_get_ns();

	spin_lock_irqsave(&thread_syscall_lock, flags);

	tp = hash_lookup(current->pid);

	if (!tp) {
		spin_unlock_irqrestore(&thread_syscall_lock, flags);
		return;
	}

	if (target_syscall != -1 && tp->sys_id != target_syscall) {
		spin_unlock_irqrestore(&thread_syscall_lock, flags);
		return;
	}
	
	duration = current_time - tp->sys_time_start;

	/* remove thread from the hashtable */
	hash_del(&tp->node);
	
	spin_unlock_irqrestore(&thread_syscall_lock, flags);

	/* update observed_times and total time */
	atomic64_add(duration, &syscall_counts[tp->sys_id].total_time);
	atomic64_inc(&syscall_counts[tp->sys_id].count);
	strscpy(sys_name, get_syscall_name(tp->sys_id), sizeof(sys_name));
	strscpy(proc_name, tp->proc_name, sizeof(proc_name));
    	trace_sys_tracer_only(
		tp->sys_id, 
		get_syscall_name(tp->sys_id), 
		duration, 
		(long long)atomic64_read(&syscall_counts[tp->sys_id].count), 
		true, 
		(long long)atomic64_read(&syscall_counts[tp->sys_id].unresolved_count),
		current->tgid,
		proc_name,
		tp->inst_point,
		tp->image_name
	);

	kfree(tp);
}








/* -----> other functions <----- */

/* function to resolve correct ip */
static void resolve_user_ip_image(struct task_struct *task, unsigned long ip, char *buf, size_t buflen)
{
	struct mm_struct *mm;		// pointer to the task address
	struct vm_area_struct *vma;	// pointer to mapping region

	/* basic check (if i have nowhere to write) */
	if (!buf || buflen == 0)
		return;

	strscpy(buf, "[unknown]", buflen); // default label

	/* access process addressspace struct
	   in case mm == null -> avoid dereferencing null pointer */
	mm = task->mm;
	if (!mm) {
		strscpy(buf, "[no-mm]", buflen);
		return;
	}

	/* lock the process mappings for reading, similar, 
	   but not the same as spinlock */
	mmap_read_lock(mm); 

	vma = find_vma(mm, ip); // to find vma 

	/* check if vma is less than vm_start or more than vm_end, meaning
	   that it does not belong to this address space, so only the ip inside 
	   start-end counts as part of that address space */
	if (!vma || ip < vma->vm_start || ip >= vma->vm_end) {
		strscpy(buf, "[no-vma]", buflen);
		goto out_unlock;
	}

	if (vma->vm_file && vma->vm_file->f_path.dentry) {
		/* basename only, e.g. libc.so.6 */
		strscpy(buf, vma->vm_file->f_path.dentry->d_name.name, buflen); // copy just a basename, not a full path
	} else {
		strscpy(buf, "[anon]", buflen); // if there is no backing file, it is anonymous address for me
	}

out_unlock:
	mmap_read_unlock(mm); // release mapping lock before returning
}

/* Hashtable lookup function */
static struct tp_thread *hash_lookup(pid_t thread_id)
{
	struct tp_thread *t;
	
	hash_for_each_possible(thread_syscall, t, node, thread_id) {
		if (t->thread_id == thread_id)
			return t;
	}

	return NULL;
}









module_init(sys_trace_init);
module_exit(sys_trace_exit);
module_param(target_pid, int, 0644);
module_param(target_syscall, int, 0644);
MODULE_LICENSE("GPL");
