/*
 * tracer.cpp
 *
 * User-space controller for the Linux kernel event tracer.
 *
 * This program loads and unloads the selected kernel module, applies
 * optional PID and syscall filters, reads aggregated results from /proc,
 * and streams event-based output from tracefs through trace_pipe.
 *
 * It also provides keyboard controls for printing aggregated output
 * and stopping the tracer cleanly.
 */

#include <iostream>	// out, cerr
#include <fstream>	// file I/O - trace_pipe, /proc
#include <string>	// string operations
#include <csignal>	// signal handling for ctrl + c
#include <cstdlib>	// system(), exit()
#include <unistd.h>	// read(), getopt()
#include <thread>	// std::thread
#include <atomic>	// std::atomic - thread safe flags
#include <termios.h>	// terminal control

std::string module_name = "";		// module selected by the user
std::string kernel_module_name = "";	// actual kernel module name (for rmmod)
std::string output_file = "";		// optional output file under flag -o
std::string proc_file = "";		// proc file for aggregated mode (set by default, but if necessary, can be set with -f flag)
std::atomic<bool> running(true);	// shared flag between threads
bool uses_tracetables = false;		// boolean values to help with logic
bool is_event_mode = false;
bool is_aggregated_mode = false;

/* Restore terminal settings */
struct termios orig_term;		// original terminal settings backup

/* Handler for Ctrl+C */
void signal_handler(int signum) {
	(void)signum;

    	std::cout << "\nStopping tracer...\n";
    	running = false; 		// tells all loops to stop

	/* check if something was loaded, than unload */
    	if (!kernel_module_name.empty()) {
        	std::string cmd = "sudo rmmod " + kernel_module_name;
        	system(cmd.c_str());
		if (uses_tracetables)
			system("sudo rmmod trace_tables");
    	}

	/* Function to restore terminal - important!! after raw mode I used through runtime of tracer */
    	tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);

    	exit(0);
}

/* Load module - decides which module to load (if any) and initialise needed variables */
bool load_module(const std::string& module, int pid, int syscall, int top_n) {
	std::string module_load = "";

	/* Resolve full module path */
	if (module == "ip_summary") {
		module_load = "aggregated_mode/ip_summary/ip_summary_mod.ko";
		kernel_module_name = "ip_summary_mod";
		is_aggregated_mode = true;
		if (proc_file.empty())
			proc_file = "/proc/ip_trace_agg";

	} else if (module == "irq_summary") {
		module_load = "aggregated_mode/irq_summary/irq_summary_mod.ko";
		kernel_module_name = "irq_summary_mod";
		is_aggregated_mode = true;
		if (proc_file == "")
			proc_file = "/proc/irq_trace_agg";

	} else if (module == "softirq_summary") {
		module_load = "aggregated_mode/softirq_summary/softirq_summary_mod.ko";
		kernel_module_name = "softirq_summary_mod";
		is_aggregated_mode = true;
		if (proc_file == "")
			proc_file = "/proc/softirq_trace_agg";

	} else if (module == "sys_summary") {
		module_load = "aggregated_mode/sys_summary/syscall_summary_mod.ko";
		kernel_module_name = "syscall_summary_mod";
		is_aggregated_mode = true;
		if (proc_file == "")
			proc_file = "/proc/sys_trace_agg";

	} else if (module == "irq_event") {
		module_load = "event_mode/irq_summary_event/irq_summary_event_mod.ko";
		kernel_module_name = "irq_summary_event_mod";
		is_event_mode = true;

	} else if (module == "softirq_event") {
		module_load = "event_mode/softirq_summary_event/softirq_summary_event_mod.ko";
		kernel_module_name = "softirq_summary_event_mod";
		is_event_mode = true;

	} else if (module == "syscall_event") {
		module_load = "event_mode/sys_summary_event/sys_summary_event_mod.ko";
		kernel_module_name = "sys_summary_event_mod";
		is_event_mode = true;

	} else {
		std::cerr << "Incorrect module selected\n";
		return false;
	}
	
	/* Create string to load in cmd */
    	std::string cmd = "sudo insmod " + module_load;

	/* If i m in syscall tracer (i can specify target pid and syscall*/
	if (module == "syscall_event" || module == "ip_summary") {
		if (pid != -1)
			cmd += " target_pid=" + std::to_string(pid);

		if (syscall != -1) {
			cmd += " target_syscall=" + std::to_string(syscall);
		}

		if (top_n != -1) {
			cmd += " top_n=" + std::to_string(top_n);
		}
		
		/* Now, call tables module to load */
		uses_tracetables = true;
		system("sudo insmod common/trace_tables/trace_tables.ko");
	}

	if (module == "sys_summary") {
		if (pid != -1)
			cmd += " target_pid=" + std::to_string(pid);

		/* Now, call tables module to load */
		uses_tracetables = true;
		system("sudo insmod common/trace_tables/trace_tables.ko");
	}

    	std::cout << "Loading module: " << cmd << std::endl;
    	system(cmd.c_str());
	
	/* Enable tracing for the particular event i have created (emission layer only) */
	if (module == "irq_event") {
		system("sudo sh -c 'echo 1 > /sys/kernel/tracing/events/irq_tracer/enable'");
	} else if (module == "softirq_event") {
		system("sudo sh -c 'echo 1 > /sys/kernel/tracing/events/softirq_tracer/enable'");
	} else if (module == "syscall_event") {
		system("sudo sh -c 'echo 1 > /sys/kernel/tracing/events/sys_tracer/enable'");
	}

	return true;
}

/* Print /proc file */
void print_proc() {
	/* Means that procfile was incorrectly defined to empty */
    	if (proc_file.empty())
        	return;

	/* /proc file to load from */
    	std::ifstream file(proc_file);
	
	/* file to write into */
	std::ofstream out;

    	if (!file.is_open()) {
        	std::cerr << "Failed to open " << proc_file << "\n";
        	return;
    	}

	if (!output_file.empty()) {
        	out.open(output_file, std::ios::app); // append mode
    	}

    	std::cout << "\n===== /proc snapshot =====\n";

    	std::string line;

    	while (std::getline(file, line)) {
        	std::cout << line << std::endl;
		if (out.is_open()) {
            		out << line << std::endl;
        	}
    	}

    	std::cout << "==========================\n";

	if (out.is_open()) {
        	out << "===== snapshot end =====\n";
    	}
}

/* Non-blocking keyboard input - runs in paralel to other executions */
void keyboard_listener() {
    	char c;
    	while (running) {
		/* this reads raw input - no enter required */
        	if (read(STDIN_FILENO, &c, 1) > 0) {
			/* if user pressed p -> means to print /proc */
            		if (c == 'p') {
                		print_proc();
			/* if user pressed q -> same as ctrl + c (sigint signal) */
            		} else if (c == 'q') {
                		raise(SIGINT);
            		}
        	}
    	}
}

/* Read trace_pipe */
void read_trace_pipe() {
	/* I have to get trace buffer through ifstream buffer into the user output */
	std::ifstream trace("/sys/kernel/tracing/trace_pipe");
	std::ofstream out;

	if (!output_file.empty()) {
        	out.open(output_file);
    	}

	if (!trace.is_open()) {
        	std::cerr << "Failed to open trace_pipe\n";
        	return;
    	}

	std::string line;
	while (running && std::getline(trace, line)) {
        	std::cout << line << std::endl;

        	if (out.is_open()) {
	            	out << line << std::endl;
        	}
    	}
}

/* Enable raw terminal mode (for keypress without enter) */
void enable_raw_mode() {
	struct termios raw;
	tcgetattr(STDIN_FILENO, &orig_term);
	raw = orig_term;

	raw.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

int main(int argc, char* argv[]) {
	signal(SIGINT, signal_handler);		// to register ctrl + c (signal) handler

	int opt;
	int pid = -1;
	int syscall = -1;
	int top_n = -1;

	/* getopt reads argumets from flags on program loadup */
	while ((opt = getopt(argc, argv, "m:p:s:o:f:n:")) != -1) {
        	switch (opt) {
            		case 'm': module_name = optarg; break;
            		case 'p': pid = std::stoi(optarg); break;
            		case 's': syscall = std::stoi(optarg); break;
            		case 'o': output_file = optarg; break;	// output file
            		case 'f': proc_file = optarg; break;	// /proc file
			case 'n': top_n = std::stoi(optarg); break;	// top_n for ip_summary (15 by default from kernel module)
            		default: std::cout << "Usage: ./tracer -m <module.ko> [-p pid] [-s syscall] [-o file] [-f /proc/file]\n";
                	return 1;
        	}
    	}

	if (module_name.empty()) {
        	std::cerr << "Module not specified!\n";
        	return 1;
	}

	if (!load_module(module_name, pid, syscall, top_n))
		return 1;

	std::cout << "Tracing... (p = print /proc (agg mode only), q = quit)\n";

	enable_raw_mode();	// to enable raw mode for key pressings (no enter required)

	std::thread key_thread(keyboard_listener);	// registration of keyboard

	if (is_event_mode)
		read_trace_pipe();	// start tracing

	key_thread.join();
	return 0;
}
