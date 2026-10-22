# Validation: 0.3.0 development milestone

Validated on 2026-10-22 using GCC 13.3.0, Meson 1.12.1, Ninja 1.13.2,
QEMU 8.2.2 TCG and Linux 6.8.0-138-generic as the disposable guest kernel.
No host init, mounts, scheduler policy or boot entries were changed.

## Results

- Native Meson/Ninja C17 build with warnings treated as errors: passed.
- Supervisor integration suite: 31 cases, 29 passed, 2 skipped because this
  execution environment forbids Unix socket creation. Both socket paths were
  exercised in the independent guest tests instead.
- Meson UndefinedBehaviorSanitizer build/suite: passed with the same two skips.
- Offline administration suite: 17 cases passed, including root confinement,
  enable/disable, safe boot generation and fallback-loader preservation.
- Meson staged install: four commands, disabled units, private EFI artifact and
  opt-in hook; no replacement init, systemctl or auto-enabled services.
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
- QEMU native services: enable/disable/start/stop/restart, stream activation and
  reactivation, credential-aware logging, account switching and runtime ownership.
- OVMF UEFI: actual Lillyboot firmware menu/default, generated entry, EFI-stub
  kernel/initrd boot through to Lilly PID 1 and orderly poweroff.
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

EFI reproduction additionally requires GNU-EFI and OVMF. Configure the VM build
with -Defi=true -Defi_vm_tests=true -Dovmf_code=/path/to/OVMF_CODE_4M.fd
-Dovmf_vars=/path/to/OVMF_VARS_4M.fd. The harness copies firmware variables and
uses a disposable FAT directory, never the machine's ESP. Static VM binaries
allow glibc NSS linker warnings; deployed native binaries remain dynamically
linked and warnings-as-errors applies to their compilation/linking.
