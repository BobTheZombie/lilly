# LILLY

**Linux Init, Login & Lifecycle System** — a modular Linux system manager in C,
started for LPM Linux, with Linux interfaces intended for other distributions.

**0.2.0-dev implements experimental PID 1 boot and service scheduling.** It has
disposable QEMU boot tests and ordinary process-lifecycle tests. It is not yet a
production desktop replacement for systemd, logind or homed. Installing Lilly
does not change the active init, bootloader, PAM policy or LPM transaction hooks.
LPM's existing multi-init choices remain available.

## Implemented

- PID 1 early mounts, console setup, cgroup v2 and orphan reaping.
- PID 1 persistence after services finish; emergency wait/reaper on fatal errors.
- Graceful dependency-ordered shutdown, deadlines, sync, unmount/read-only remount
  attempts and real kernel poweroff/reboot.
- Deterministic dependency planning with Requires selection, After ordering,
  missing requirement errors and cycle detection.
- Parallel nonblocking fork/exec activation and honest exec failure reporting.
- Simple/oneshot services, configurable start/stop timeouts, bounded restart
  windows and runtime hard-dependency failure propagation.
- Per-service FIFO/RR/normal/batch/idle scheduling, priorities, CPU affinity, nice,
  RT CPU runtime limits and reset-on-fork. PID 1 remains normally scheduled.
- Local credential-checked control socket for status, poweroff and reboot.
- Strict parsing: unsupported security/privilege directives fail validation.
- LPM recipe, staged installation, host tests and QEMU/TCG boot CI.

## Build and try

Linux, a C17 compiler, Meson (>=1.3) and Ninja are required. Python 3 is
used by the build system and tests; it is not a runtime dependency of Lilly.
The executable links only against libc. Service commands use explicit argv
parsing, with no shell expansion unless an administrator explicitly runs a shell.

```sh
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
build/lilly verify examples/system
build/lilly plan examples/system
# Small foreground example (no host init change):
mkdir -p /tmp/lilly-demo
cp examples/hello.service /tmp/lilly-demo/
build/lilly run /tmp/lilly-demo
meson install -C build --destdir "$PWD/stage"
```

```text
lilly --help
lilly --version
lilly verify UNIT_DIRECTORY [SERVICE]
lilly plan UNIT_DIRECTORY [SERVICE]
lilly run UNIT_DIRECTORY [SERVICE]
lilly init [UNIT_DIRECTORY]                 # PID 1 only
lilly ctl SOCKET {status|stop|poweroff|reboot}
```

PID 1 defaults to `/etc/lilly/services`. Every `.service` in that directory is
selected. Outside PID 1 the directory is explicit. An optional SERVICE selects
only that service and its Requires closure. After orders selected units without
pulling them in; Requires does not imply After. `Type=simple` means successful
exec, not application readiness. There is no notify/socket activation yet.

Service files are trusted administrator code. Foreground services inherit the
caller's credentials, environment, working directory and output. User/Group,
sandboxing and privilege dropping are not implemented; unsupported directives
are rejected. Do not run untrusted units as root. CPU controls are not a security
sandbox or a hard real-time latency guarantee.

Read [PID 1 setup and VM tests](docs/PID1.md),
[scheduling](docs/SCHEDULING.md), [compatibility](docs/COMPATIBILITY.md),
[architecture](docs/ARCHITECTURE.md) and [remaining work](docs/ROADMAP.md).
