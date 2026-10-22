# Architecture and engineering rules

Parsing/planning and the event loop live in src/lilly.c. Privileged early boot,
cgroup membership, emergency recovery and final shutdown live in src/platform.c.
Use C17, Linux APIs and explicit error paths; do not introduce a language runtime
into the eventual PID 1. Administrative utilities can have optional libraries.

## Service lifecycle

A selected service starts only after selected After predecessors have either
completed (oneshot) or successfully exec'd (simple). Exec failure is a failed
start, never a successful activation. A final failed Requires dependency blocks/stops its dependents; After alone
only orders and does not propagate failure. SIGCHLD is blocked before fork and consumed via signalfd to avoid a
lost-reaping race. Children have their own process group and unblock signals
before exec. The parent registers as a child subreaper for foreground testing.

Restart attempts have configurable fixed-window limits, delays and start/stop
timeouts. Runtime Requires failure propagates. PID 1 places services in cgroup v2
so descendants remain contained across setsid; foreground mode uses process
groups only. Readiness and further availability semantics are still pending.

## Privileged boundary

A bounded local control socket checks SO_PEERCRED; root is required for PID 1.
There is no network listener or remote privileged API. Future control operations must check
peer credentials, bound request sizes, authorize each action and version its
protocol. Never expose a root shell as a service-management API. Unsupported
security settings must fail validation. Readiness and liveness are distinct.

## PID 1 milestone

Boot tests now run the real binary in a disposable QEMU guest from an initramfs. Provide mount
setup, console, signal handling, kernel command-line parsing, failure recovery,
rescue, bounded shutdown, sync and orderly unmount before enabling installation
as init. Test kernel panic/reboot behavior, interrupted boot, missing root,
missing loader and read-only root. Do not infer bootability from foreground tests.

## Accounts, temporary files and boot

Account and filesystem operations need their own parsers and offline-root tests.
Use directory-fd operations and no-follow resolution for privileged writes.
Accounts require locks, staged replacements, fsync and recovery from partial
multi-file updates. Keep boot entries and EFI variables as separate operations;
never delete other loaders or enroll signing keys implicitly.

## LPM integration

Keep package installation, dependency/provider selection, hooks and rollback in
LPM. Lilly manages runtime lifecycle. An init activation transaction must retain
a bootable previous option and regenerate the matching initramfs; merely creating
an init symlink is insufficient. Kernel/ESP hooks must select an actual provider
and must not assume systemctl is the only available implementation.
