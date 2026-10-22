# Architecture and engineering rules

The current executable keeps parsing/planning and process lifecycle in one small
translation unit. Split into libraries when a second real consumer exists.
Use C17, Linux APIs and explicit error paths; do not introduce a language runtime
into the eventual PID 1. Administrative utilities can have optional libraries.

## Service lifecycle

A selected service starts only after selected After predecessors have either
completed (oneshot) or successfully exec'd (simple). Exec failure is a failed
start, never a successful activation. A failed ordered predecessor blocks the
dependent. SIGCHLD is blocked before fork and consumed via signalfd to avoid a
lost-reaping race. Children have their own process group and unblock signals
before exec. The parent registers as a child subreaper for foreground testing.

Restart attempts are bounded for the complete invocation. Production work must
add configurable sliding-window rate limits, backoff, timeouts, readiness,
runtime failure propagation and cgroup ownership before claiming availability.
Forked processes that escape the service's process group are not contained.

## Privileged boundary

The first release has no privileged control API. A later Unix socket must check
peer credentials, bound request sizes, authorize each action and version its
protocol. Never expose a root shell as a service-management API. Unsupported
security settings must fail validation. Readiness and liveness are distinct.

## PID 1 milestone

Bring up in a disposable QEMU guest, initially from an initramfs. Provide mount
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
