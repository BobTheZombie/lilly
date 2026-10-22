# Validation: 0.2.0 development milestone

Validated on 2026-10-22 using GCC 13.3.0, Meson 1.12.1, Ninja 1.13.2,
QEMU 8.2.2 TCG and Linux 6.8.0-138-generic as the disposable guest kernel.
No host init, mounts, scheduler policy or boot entries were changed.

## Results

- Native Meson/Ninja C17 build with warnings treated as errors: passed.
- Supervisor integration suite: 28 cases, 26 passed, 2 skipped because this
  execution environment forbids Unix socket creation. Both socket paths were
  exercised in the independent guest tests instead.
- Meson UndefinedBehaviorSanitizer build/suite: passed with the same two skips.
- Meson staged install: passed; runtime executable and docs/examples only,
  no replacement init, systemctl, active units, bootloader files or hooks.
- QEMU boot: passed actual PID 1 early mounts, console device nodes, cgroup v2,
  parallel services, real RR scheduling at priority 10 on CPU 0, RT runtime kill,
  reset-on-fork, cleanup of a setsid descendant, status/control and poweroff.
- QEMU control checks: delayed request handled, non-root peer rejected even
  with permissive socket filesystem mode, existing socket not overwritten.
- QEMU idle: PID 1 remained alive after all native service commands completed,
  reaped an orphan helper and handled external TERM for orderly poweroff.
- QEMU invalid configuration: emergency PID 1 remained alive and accepted TERM
  for shutdown; no kernel panic.
- QEMU reboot: accepted reboot request, completed shutdown and rebooted the guest.
- LPM recipe shell syntax and native example unit verification: passed.

Commands to reproduce from a fresh checkout:

```sh
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
meson install -C build --destdir "$PWD/stage"
meson setup build-ubsan -Db_sanitize=undefined
meson test -C build-ubsan --print-errorlogs
meson setup build-vm -Dvm_tests=true \
  -Dvm_kernel=/path/to/vmlinuz -Dvm_busybox=/path/to/static/busybox
meson compile -C build-vm
meson test -C build-vm --print-errorlogs
```

The workflow repeats native/sanitizer tests with GCC and Clang, then boots the
static test executable under QEMU. QEMU tests use a generated initramfs, no host
disk or network. This validates the early PID 1 milestone, not every machine's
storage, real-root initramfs handoff, hardware/desktop integration or hard
real-time latency. Real-root filesystem unmount and storage teardown still need
additional disk-backed VM coverage before production activation.
