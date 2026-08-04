/* Module Licence - GPL
 * 
 * Ondrej Hurinek
 * Instruction pointer tracing module - aggregation layer
 *
 * This module attaches the probes into sys_enter and sys_exit tracepoints and
 * measures the highest frequency of instructions executing systemcalls
 * 
 * Data are than exposed using /proc subsystem
 *
 */






#include <asm/unistd.h>			// __NR_syscalls
#include <linux/module.h> 		// 
#include <linux/init.h> 		// init/exit macros
#include <linux/string.h> 		// strcmp
#include <linux/tracepoint.h>		// tracepoint structs
#include <linux/mm.h> 			// for memory management i need to resolve, where the ip came from, also includes memory map functions
#include <linux/fs.h>			// struct file and inode
#include <linux/dcache.h> 		// to get the name of the file that has the instruction pointer
#include <linux/spinlock.h>		// spinlock
#include <linux/sched.h>		// for 'current' use
#include <linux/slab.h> 		// kernel heap allocator
#include <linux/hashtable.h>		// hashtable
#include <linux/ptrace.h> 		// to read ip from regs using instruction_pointer(regs)
#include <linux/proc_fs.h>		// proc subsystem
#include <linux/seq_file.h>		// seq_file into proc
#include <linux/sort.h> 		// sort function
#include "../../common/trace_common/tp_lookup.h" // tracepoint lookup functions
#include "../../common/trace_tables/trace_tables.h" // to include defined tables






/* some forward declarations */
static int print_ip(struct seq_file *m, void *v);
static int bridge_print_ip(struct inode *inode, struct file *file);
static int cmp_hot_entries(const void *a, const void *b);
static void sys_trace_enter(void *data, struct pt_regs *regs, long id);
static struct sys_entered *hash_lookup(pid_t tgid, long sys_id, unsigned long ip);
static void resolve_user_ip_image(struct task_struct *task, unsigned long ip, char *buf, size_t buflen, unsigned long *offset);

static DEFINE_SPINLOCK(ip_lock); 	// spinlock

/* Struct for proc operations */
static const struct proc_ops ip_proc_ops = {
	.proc_open 	= bridge_print_ip,
	.proc_read 	= seq_read,
	.proc_lseek 	= seq_lseek,
	.proc_release	= single_release,
};

/* struct for hashtable */
struct sys_entered {
	pid_t tgid;			// process id
	long sys_id;			// systemcall id
	unsigned long ip;		// instruction pointer
	u64 count;			// count of syscall occurences
	char comm[TASK_COMM_LEN];	// command name - basically the process that calls the program that calls the instruction
	char image_name[64]; 		// exact file that called instruction
	unsigned long offset; 		// exact ofset of the instruction
	struct hlist_node node;
};

/* Tracepoint structs to work with */
static struct tracepoint *tp_sys_enter;

/* Entry for /proc_fs */
static struct proc_dir_entry *ip_proc_entry;

/* Hashtable to store instructions */
DEFINE_HASHTABLE(ip_results, 10);

/* Variable to hold target_pid, -1 by default since
   tgid can never be -1, so if -1, means i m printing
   all of the system calls, othervise, target_pid loaded
   at the module loading */
static int target_pid = -1;

/* variable to hold sys traget */
static int target_syscall = -1;		// variable to hold systemcall target
static int top_n = 15;			// number of instruction to display






/* -----> probe functions <----- */

static void sys_trace_enter(void *data, struct pt_regs *regs, long id)
{
	struct sys_entered *ip_entered;
	struct sys_entered *new_ip_entered;
	unsigned long flags;
	unsigned long ip;
	unsigned long key;

	(void)data;

	/* if id out of range of number of syscalls */
	if (id < 0 || id >= __NR_syscalls)
		return;

	/* target syscall defined */
	if (target_syscall != -1 && id != target_syscall)
		return;

	/* target process id defined */
	if (target_pid != -1 && current->tgid != target_pid)
		return;

	ip = instruction_pointer(regs);
	key = ip ^ ((unsigned long)id << 8) ^ ((unsigned long)current->tgid << 16);

	spin_lock_irqsave(&ip_lock, flags);

	/* look for this ip inside hashtable */
	ip_entered = hash_lookup(current->tgid, id, ip);

	/* if this ip already exist in hashtable */
	if (ip_entered) {
		ip_entered->count++;
		spin_unlock_irqrestore(&ip_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&ip_lock, flags);

	/* allocate new_ip */
	new_ip_entered = kmalloc(sizeof(*new_ip_entered), GFP_ATOMIC);
	if (!new_ip_entered)
		return;

	new_ip_entered->offset = 0;
	new_ip_entered->tgid = current->tgid;
	new_ip_entered->sys_id = id;
	new_ip_entered->ip = ip;
	new_ip_entered->count = 1;
	strscpy(new_ip_entered->comm, current->comm, sizeof(new_ip_entered->comm));

	/* resolve only when both filters are set, and only for new entries */
	if (target_pid != -1 && target_syscall != -1)
		resolve_user_ip_image(
			current, 
			ip,
			new_ip_entered->image_name,
			sizeof(new_ip_entered->image_name),
			&new_ip_entered->offset);
	else
		strscpy(new_ip_entered->image_name, "unresolved",
			sizeof(new_ip_entered->image_name));

	spin_lock_irqsave(&ip_lock, flags);

	hash_add(ip_results, &new_ip_entered->node, key);
	spin_unlock_irqrestore(&ip_lock, flags);
}







/* load and unload the module */

static int __init sys_trace_init(void)
{
    	int ret;

	/* init thread hashtable and syscall_counts[__NR_syscalls] */
	hash_init(ip_results);

	/* initialise probe to the tracepoints */
	tp_sys_enter = find_tracepoint_by_name("sys_enter");
	if (!tp_sys_enter) {
		pr_err("tracepoint 'sys_enter' not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_sys_enter, sys_trace_enter, NULL);
	if (ret) {
		pr_err("sys_enter failed: %d\n", ret);
		return ret;
	}

	/* make array avalaible in correct format in user space */
	ip_proc_entry = proc_create("ip_trace_agg", 0444, NULL, &ip_proc_ops);
	if (!ip_proc_entry) {
		tracepoint_probe_unregister(tp_sys_enter, sys_trace_enter, NULL);
		return -ENOMEM;
	}

	return 0;
}

static void __exit sys_trace_exit(void)
{
	struct sys_entered *e;
	struct hlist_node *tmp;
	int bkt;

	if (ip_proc_entry) {
		proc_remove(ip_proc_entry);
		ip_proc_entry = NULL;
	}

	if (tp_sys_enter) {
		tracepoint_probe_unregister(tp_sys_enter, sys_trace_enter, NULL);
		tp_sys_enter = NULL;
	}

	tracepoint_synchronize_unregister();

	hash_for_each_safe(ip_results, bkt, tmp, e, node) {
		hash_del(&e->node);
		kfree(e);
	}
}







/* -----> proc functions <----- */

/* function to print results using /proc file system */
static int print_ip(struct seq_file *f, void *v)
{
	struct sys_entered *e;			// pointer to the struct
	struct sys_entered **arr = NULL;	// pointer to array of structs
	unsigned long flags;
	int count = 0;
	int i;
	int j;
	int limit;
	int bkt;

	/* count number of stored instruction pointers */
	spin_lock_irqsave(&ip_lock, flags);
	hash_for_each(ip_results, bkt, e, node)
		count++;
	spin_unlock_irqrestore(&ip_lock, flags);

	if (count == 0) {
		seq_puts(f, "No entries.\n");
		return 0;
	}

	/* allocate array of structs */
	arr = kmalloc_array(count, sizeof(*arr), GFP_KERNEL);
	if (!arr)
		return -ENOMEM;

	/* initialise array */
	spin_lock_irqsave(&ip_lock, flags);
	hash_for_each(ip_results, bkt, e, node) {
		if (i < count)
			arr[i++] = e;
	}
	spin_unlock_irqrestore(&ip_lock, flags);

	/* sort array */
	sort(arr, count, sizeof(*arr), cmp_hot_entries, NULL);

	/* set the limit */
	limit = (top_n > 0 && top_n < count) ? top_n : count;
	
	seq_printf(f, "Top %d hottest syscall call-sites\n", limit);	// header

	for (j = 0; j < 160; j++) {					// dashes
		seq_printf(f, "-");
	}
	seq_printf(f, "\n");

	seq_printf(f, "%7s | %7s | %15s | %10s | %25s | %15s | %15s | %15s | %15s\n",
		"rank", "count", "pid", "syscall", "syscall name", "ip", "offset", "comm", "image");	// dashes

	for (j = 0; j < 160; j++) {					// dashes
		seq_printf(f, "-");
	}
	seq_printf(f, "\n");

	for (i = 0; i < limit; i++) {
		seq_printf(f, "%7d | %7llu | %15d | %10ld | %25s | 0x%13lx | 0x%13lx | %15s | %15s\n",
			   i + 1,
			   (unsigned long long)arr[i]->count,
			   arr[i]->tgid,
			   arr[i]->sys_id,
			   get_syscall_name(arr[i]->sys_id),
			   arr[i]->ip,
			   arr[i]->offset,
			   arr[i]->comm,
			   arr[i]->image_name);
		}

	kfree(arr);
	return 0;
}

/* Function to bridge print_sys with the function that creates proc file
   !!must be this signature!! */
static int bridge_print_ip(struct inode *inode, struct file *file)
{
	return single_open(file, print_ip, NULL);
}







/* -----> other functions <----- */

/* Hashtable lookup function */
static struct sys_entered *hash_lookup(pid_t tgid, long sys_id, unsigned long ip)
{
	struct sys_entered *se;
	unsigned long key = ip ^ ((unsigned long)sys_id << 8) ^ ((unsigned long)tgid << 16);
	
	hash_for_each_possible(ip_results, se, node, key) {
		if (	se->tgid == tgid &&
			se->sys_id == sys_id &&
			se->ip == ip)
			return se;
	}

	return NULL;
}

/* function to resolve correct ip */
static void resolve_user_ip_image(struct task_struct *task, unsigned long ip, char *buf, size_t buflen, unsigned long *offset)
{
	struct mm_struct *mm; // pointer to the task address
	struct vm_area_struct *vma; // pointer to mapping region

	// basic check (if i have nowhere to write)
	if (!buf || buflen == 0)
		return;

	strscpy(buf, "[unknown]", buflen); // default label

	mm = task->mm; // access proccess addressspace struct
	// in case mm == null -> to avoid dereferencing null pointer
	if (!mm) {
		strscpy(buf, "[no-mm]", buflen);
		return;
	}

	mmap_read_lock(mm); // lock the process mappings for reading, similar, but not the same as spinlock

	vma = find_vma(mm, ip); // to find vma 
	/* check if vma is less than vm_start or more than vm_end, meaning
	   that it does not belong to this address space, so only the ip inside start-end 
	   counts as part of that address space*/
	if (!vma || ip < vma->vm_start || ip >= vma->vm_end) {
		strscpy(buf, "[no-vma]", buflen);
		goto out_unlock;
	}

	if (vma->vm_file && vma->vm_file->f_path.dentry) {
		/* basename only, e.g. libc.so.6 */
		strscpy(buf, vma->vm_file->f_path.dentry->d_name.name, buflen); // copy just a basename, not a full path
		*offset = (ip - vma->vm_start) + ((unsigned long)vma->vm_pgoff << PAGE_SHIFT);
	} else {
		strscpy(buf, "[anon]", buflen); // if there is no backing file, it is anonymous address for me
		*offset = ip - vma->vm_start;
	}

out_unlock:
	mmap_read_unlock(mm); // release mapping lock before returning
}

/* function to compare structs, and return for sort function */
static int cmp_hot_entries(const void *a, const void *b)
{
	const struct sys_entered * const *ea = a;
	const struct sys_entered * const *eb = b;

	if ((*ea)->count < (*eb)->count)
		return 1;
	if ((*ea)->count > (*eb)->count)
		return -1;
	return 0;
}







module_param(target_pid, int, 0644);
module_param(target_syscall, int, 0644);
module_param(top_n, int, 0644);
module_init(sys_trace_init);
module_exit(sys_trace_exit);
MODULE_LICENSE("GPL");
