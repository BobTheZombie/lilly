# Validation: 0.5.0 development milestone

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

## Device/PAM integration milestone

Seven native provider-adapter tests cover command order, failed-trigger short
circuiting, deadline termination, missing executables, literal argv forwarding,
argument validation and foreground daemon execution. The PAM fixture requires
root and audit netlink; on restricted hosts it reports a skip.

The independent devices-pam-vm case runs a real systemd-udevd/udevadm 255 provider
with Linux-PAM 1.5.3 and util-linux runuser, under Lilly PID 1. It tests real
virtio disk coldplug, rule properties/symlinks, rule reload, PCI hot-removal and
symlink cleanup; concurrent PAM sessions, metadata escaping, environment, private
runtime permissions, close/pam_end cleanup and symlink rejection; then a real
runuser nobody login and clean shutdown. No host device daemon, account policy
or device is changed. This validates this provider/version, not all eudev builds
or complete systemd-udevd/logind compatibility.

To enable this additional guest, use -Dpam=enabled -Ddevice_vm_tests=true with
the existing vm_tests/kernel/busybox options and set vm_udevd/vm_udevadm to trusted
matching installed provider executables. The fixture copies their dynamic
libraries plus installed PAM modules/runuser. A kernel with built-in virtio PCI
and virtio block support is required; no host kernel modules are reused.

0.4 verification: native GCC and UBSan suites passed (seven device-adapter
cases plus prior supervisor/administration suites; PAM and two Unix-socket
cases skip under this restricted host). All five existing PID 1 guests and the
new real-provider/PAM/util-linux guest passed. The OVMF EFI regression also
passed. Staged installation includes pam_lilly.so with exported session entry
points and a valid native catalog, with no active PAM-policy or init replacement.

## Standalone native engine profile

Pinned udev 257.10 and elogind/login1 256 were built from C sources through the
Meson/Ninja profile and staged with private RPATHs, full generated rules/data,
compiled private hwdb, client libraries and source/license provenance. No host
device/session daemon or PAM policy was changed. Both the direct build driver
and parent Meson compile/install path were exercised.

The native guest validates rule ATTR matching, GOTO/LABEL flow, PROGRAM/RESULT,
program and hwdb IMPORT, ENV append, final MODE assignment, SYMLINK and RUN;
real login1 D-Bus introspection; inhibitor-FD lifetime; a full PAM-created nobody
session and its properties; denied unprivileged PowerOff; and authorized D-Bus
PowerOff routed through servicectl to Lilly PID 1. This is actual engine code,
not an IPC mock. Hardware seat/DRM handoff and real suspend are not validated by
this guest.

The PID 1 suite additionally checks Halt reaches RB_HALT_SYSTEM and the kernel's
System halted state; QEMU is then stopped after observing that terminal state.
This distinguishes Halt from PowerOff. Native-engine CI repeats the full build,
stage, host tests and guest boot directly from the tracked source trees. Enable the extra guest with
-Dnative_engine_vm_tests=true -Dnative_engine_stage=/absolute/stage plus vm_tests
options, and install into that staging directory before running it.
