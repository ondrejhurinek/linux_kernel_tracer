# Usage Guide

## License

GPL

---

## Known Limitation

Instruction pointer symbol resolution is currently incomplete.

---

## Command-Line Options

| Option | Description |
|---------|-------------|
| `-m` | Kernel module to load |
| `-p` | Process ID filter |
| `-s` | System call filter |
| `-o` | Output file |
| `-n` | Number of instruction pointers displayed |
| `-f` | Alternative `/proc` file |

---

## Example

```bash
./tracer.out -m ip_summary -p 5028 -s 0 -o output_file.txt -n 20
```

---

## Controls

- `p` — Toggle aggregated output
- `q` — Quit
- `Ctrl+C` — Quit

---

## Available Modules

### Aggregated Profiling

| Module | Description | Options |
|---------|-------------|---------|
| `ip_summary` | Displays hottest instruction pointers | `-p -s -n -o` |
| `irq_summary` | Displays interrupt statistics | `-o` |
| `softirq_summary` | Displays software interrupt statistics | `-o` |
| `sys_summary` | Displays system call statistics | `-o` |

### Event Tracing

| Module | Description | Options |
|---------|-------------|---------|
| `irq_event` | Runtime hardware interrupt tracing | `-o` |
| `softirq_event` | Runtime software interrupt tracing | `-o` |
| `syscall_event` | Runtime system call tracing | `-p -s -o` |

---

## tracefs

Event mode reads emitted events through the Linux **tracefs**
interface (`/sys/kernel/tracing/trace_pipe`).

Since `tracefs` is a global tracing subsystem, it is recommended to
clear the tracing buffer before starting a new tracing session.

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
