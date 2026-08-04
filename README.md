# Lightweight Kernel-Level Event Tracer and Profiler for Linux

A lightweight Linux kernel event tracer and profiler implemented using
Linux kernel modules and Linux tracepoints.

The tracer monitors kernel activity by tracing **system calls (syscalls)**,
**hardware interrupts (IRQs)**, **software interrupts (SoftIRQs)** and
instruction pointer activity. It provides both **event-driven** and
**aggregated profiling** modes for analysing runtime kernel behaviour.

Unlike higher-level tracing frameworks, this project interacts directly
with Linux kernel tracing infrastructure to provide a deeper understanding
of Linux kernel execution and event processing.

---

## Features

- Linux kernel modules written in C
- Linux tracepoint-based tracing
- System call (syscall) tracing
- Hardware interrupt (IRQ) tracing
- Software interrupt (SoftIRQ) tracing
- Instruction pointer profiling
- Event-driven tracing
- Aggregated profiling
- Runtime filtering by process ID and syscall
- Integration with Linux tracefs

---

## Kernel Technologies

- C
- Linux Kernel Modules
- Linux Tracepoints
- tracefs

---

## Repository Structure

```
modules/
├── ip_summary/
├── irq_summary/
├── softirq_summary/
├── sys_summary/
├── irq_event/
├── softirq_event/
└── syscall_event/

loader/
```

---

## Available Modules

### Aggregated Profiling

| Module | Description |
|---------|-------------|
| ip_summary | Displays hottest instruction pointers |
| irq_summary | Displays interrupt statistics |
| softirq_summary | Displays software interrupt statistics |
| sys_summary | Displays system call statistics |

### Event Tracing

| Module | Description |
|---------|-------------|
| irq_event | Runtime hardware interrupt tracing |
| softirq_event | Runtime software interrupt tracing |
| syscall_event | Runtime system call tracing |

---

## Building

Compile the kernel modules using the provided Makefiles.

Load the desired module through the supplied loader application.

---

## Usage

Example:

```bash
./tracer.out -m ip_summary -p 5028 -s 0 -o output.txt -n 20
```

Press:

```
p    Toggle aggregated output
q    Quit
Ctrl+C    Quit
```

---

## Command-Line Options

| Option | Description |
|---------|-------------|
| `-m` | Kernel module to load |
| `-p` | Filter by process ID |
| `-s` | Filter by syscall number |
| `-o` | Output file |
| `-n` | Number of instruction pointers displayed |
| `-f` | Alternative `/proc` file |

---

## tracefs

Event mode reads emitted kernel events through the Linux **tracefs**
interface (`/sys/kernel/tracing/trace_pipe`).

Because tracefs is a global tracing subsystem, it is recommended to clear
the tracing buffer before starting a new tracing session.

Disable tracing:

```bash
echo 0 | sudo tee /sys/kernel/tracing/tracing_on
```

Disable all enabled events:

```bash
echo 0 | sudo tee /sys/kernel/tracing/events/enable
```

Clear the trace buffer:

```bash
echo | sudo tee /sys/kernel/tracing/trace
```

Re-enable tracing:

```bash
echo 1 | sudo tee /sys/kernel/tracing/tracing_on
```

---

## Known Limitations

- Instruction pointer symbol resolution is currently incomplete.

---

## Future Work

- Improve instruction pointer symbol resolution.
- Support additional Linux tracepoints.
- Extend runtime filtering.
- Improve performance profiling capabilities.

---

## License

GPL
