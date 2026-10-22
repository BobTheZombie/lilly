# Booting Lilly as PID 1

This milestone is **experimental PID 1**, with disposable QEMU coverage. It is
not yet a production desktop replacement for systemd/logind/homed. Installing
the LPM package does not replace init or enable any service. Keep another tested
boot entry while validating a machine-specific root filesystem and initramfs.

## Supported boot contract

Run `/usr/bin/lilly` as PID 1 with no arguments, or `lilly init [UNIT_DIRECTORY]`.
The native catalog is /etc/lilly/system plus /usr/lib/lilly/system, selected by
/etc/lilly/enabled markers. If the legacy /etc/lilly/services directory exists,
its select-all behavior takes precedence. Explicit UNIT_DIRECTORY uses legacy mode. `lilly init` refuses to
run outside PID 1. There is no implicit loading of systemd vendor services.

Lilly expects a usable root filesystem, executable and ELF loader already
available. An initramfs that mounts the real root must hand over with its own
`switch_root NEWROOT /usr/bin/lilly`. Lilly does not parse root=, unlock storage,
assemble RAID or switch root itself. A systemd-based initramfs must be replaced
or adapted explicitly before trying this provider. Kernel `init=/usr/bin/lilly`
only works when the kernel/early userspace can reach that root and executable.

Early setup makes mounts private, mounts proc/sysfs/devtmpfs/devpts/tmpfs for
/run and cgroup v2, opens /dev/console, and creates /run/lilly. A matching existing
mount is retained; a directory merely having tmpfs filesystem magic does not
count as a mounted devtmpfs or /run. Device discovery, network, fstab mounts,
login/PAM, D-Bus and desktop services remain external services. cgroup v2 and
cgroup.kill (Linux 5.14+) are required. No cgroup controller resource limits are
implemented yet. Per-service cgroups contain descendants even after setsid.

For a real root, create reviewed native units using `examples/system` as a
starting point. Verify executable paths, foreground flags, PAM login, runtime
users/directories, D-Bus setup, fstab/fsck and networking before booting. The
examples are not auto-enabled and do not provision the machine. eudev's udevd
path can differ by distribution. Mount-a does not perform a full fsck/swap/root
remount policy. No promise of a full desktop boot is made from the small VM.

## Administrative control

```sh
lilly ctl /run/lilly/control status
lilly ctl /run/lilly/control poweroff
lilly ctl /run/lilly/control reboot
```

The socket is Unix SOCK_SEQPACKET, created under a private directory. Requests
are authorized using SO_PEERCRED: root only for PID 1, the supervisor's effective
UID in foreground mode. The socket path is never overwritten. Clients have a
one-second request deadline and a bounded 16-client pending pool; idle clients
do not block PID 1. Control clients time out after five seconds. servicectl implements per-unit start/stop/restart and persistent enable/disable.
Live catalog reload is not implemented. See SERVICES.md. Foreground tests can set
`LILLY_CONTROL_SOCKET` explicitly and use the `stop` action to exit the supervisor.
PID 1 rejects `stop` because it must not exit.

SIGTERM, SIGUSR2 and SIGPWR initiate poweroff; SIGINT initiates reboot (including
Ctrl-Alt-Delete after the kernel's immediate-reset behavior is disabled).
SIGUSR1 logs a summary. SIGHUP logs that reload is unsupported. Signalfd consumes
these signals; handlers are installed so namespace PID 1 receives them too.

PID 1 remains alive when all services complete or fail. Parsing/resource errors
enter an emergency wait/reaper loop rather than exiting and panicking the kernel.
Emergency mode logs on the console and accepts the shutdown/reboot signals; it
does not expose an unauthenticated root shell and does not service the control
socket. Use a separate rescue boot entry for repairing a broken configuration.

Shutdown waits for services in reverse After order with individual TERM/KILL
deadlines, then terminates and reaps remaining processes. It syncs, attempts
unmounts deepest first with retries, remounts remaining writable filesystems and
root read-only where supported, syncs again and calls reboot/poweroff. Failures
are logged. Exotic filesystems, network mounts, uninterruptible I/O and encrypted
storage teardown require further testing; this is not a full shutdown-initramfs.

## Reproducible VM test

On an Ubuntu/Debian test machine, install QEMU, a bootable x86-64 Linux kernel,
a static BusyBox and a static libc development toolchain. Then:

```sh
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
meson setup build-vm -Dvm_tests=true \
    -Dvm_kernel=/path/to/vmlinuz -Dvm_busybox=/path/to/static/busybox
meson compile -C build-vm
meson test -C build-vm --suite vm --print-errorlogs
```

The test creates its initramfs with a Python newc writer. It uses QEMU TCG with
no networking or host disks. It boots Lilly as PID 1, verifies early devices,
real RR scheduling/affinity/runtime limit/reset-on-fork, escaped descendant
cleanup through cgroups, control status and poweroff. Additional guests verify
idle PID 1 persistence, invalid-config emergency recovery, and orderly reboot.
Logs remain in the selected build directory under `vm-logs/`. No host mount or init change is performed.
