# Service and real-time scheduling

Lilly launches all ready services without waiting for another service's exec
handshake. `After` gates activation until a predecessor has exec'd (simple) or
completed (oneshot). `Requires` selects the hard dependency and propagates final
failure to its dependents; it does not imply ordering. This is event-driven
startup scheduling, independent of the kernel CPU scheduler.

Lilly/PID 1 stays under the normal Linux scheduling policy. A service may opt
into the following `[Service]` directives:

| Directive | Supported values | Default |
| --- | --- | --- |
| CPUSchedulingPolicy | other, batch, idle, fifo, rr | other |
| CPUSchedulingPriority | 1..99 for fifo/rr; 0 for other policies | 0 |
| CPUAffinity | CPU numbers/ranges separated by spaces or commas; 0..CPU_SETSIZE-1 | inherited allowed CPUs |
| Nice | -20..19 | inherited |
| LimitRTTIME | CPU microseconds, integer 1000..1000000 | 200000 for RT services |
| TimeoutStartSec | integer 1..3600 seconds | 30 |
| TimeoutStopSec | integer 1..300 seconds | 3 |
| Restart | no, on-failure, always; restarting oneshots rejected | no |
| RestartSec | integer 0..3600 seconds | 0.25 |

`StartLimitBurst` (1..1000, default 5) and `StartLimitIntervalSec` (1..86400,
default 10) live in `[Unit]`. These use a fixed monotonic interval window. A
burst exhausted within that window is a final failure; no automatic retry after
rate-limit exhaustion is scheduled. `RestartSec=0` is still bounded by this limit.

Linux permissions and the host's allowed CPU set apply. Lilly fails the service
if affinity, nice, limits or scheduler setup fails; it never pretends an RT
request succeeded. RT policies need the appropriate capabilities/resource limits.
`SCHED_RESET_ON_FORK` prevents children from inheriting FIFO/RR accidentally.
`RLIMIT_RTTIME` soft and hard limits are set equally for RT services; a CPU-bound
RT process is killed when it exhausts this budget without blocking. A service
that blocks periodically can remain running. Trusted root services can change
their own settings; these controls are not a security sandbox.

See `examples/realtime.service`. FIFO/RR do not make Linux a hard real-time OS.
Kernel preemption, IRQs, drivers, RT throttling and CPU isolation still determine
latency. No SCHED_DEADLINE admission control or latency guarantee is claimed.
PID 1's own event loop uses monotonic timerfd ticks (20ms) and signalfd, not RT
priority. CPU policy configuration is verified in a QEMU guest by querying the
running process, checking reset-on-fork and exhausting an RT CPU budget.

Service start timeouts include pre-exec setup and the complete oneshot command.
A simple service is considered started on successful exec, not application
readiness. Timeout causes TERM, then KILL after TimeoutStopSec, and is a failed
attempt for restart policy purposes. Shutdown sends TERM in reverse After order,
waiting for ordered dependents to exit before terminating their prerequisites.
An overall five-minute deadline bounds the service shutdown phase.

Source contracts:
- https://man7.org/linux/man-pages/man2/sched_setscheduler.2.html
- https://man7.org/linux/man-pages/man7/sched.7.html
- https://man7.org/linux/man-pages/man2/getrlimit.2.html
