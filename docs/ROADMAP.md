# Milestones

1. **Implemented: C service-supervision foundation.** Strict parser, dependency
   plan, parallel activation, bounded restarts, timeouts and runtime Requires
   failure propagation.
2. **Implemented: experimental PID 1 and CPU policy controls.** Early mounts,
   console, cgroup v2, reaping, persistent event loop, emergency mode, authenticated
   local control and shutdown/reboot. QEMU checks actual RR/affinity/reset-on-fork,
   runaway RT budget, escaped-child cleanup, idle and broken-config persistence.
3. **Next: complete lifecycle and machine integration.** Notify/socket activation,
   readiness-aware dependency startup, per-unit start/stop/reload control, cgroup
   resource controllers, environment/privilege/sandbox settings, installable
   reviewed boot profiles, initramfs root handoff integration and more storage/
   shutdown tests. Keep activation separate from package installation.
4. **lilly-sysusers and lilly-tmpfiles.** Implement documented compatible subsets,
   offline root, precedence, locking, transactional writes and hostile path tests.
5. **Desktop compatibility.** Notify/socket activation, D-Bus service lifecycle,
   PAM login integration, elogind interoperability, UPower and
   power-profiles-daemon. Reuse device/network daemons rather than invent shims.
6. **lilly-userdb and lilly-homed.** NSS, Varlink, JSON records and PAM contracts;
   encrypted managed homes only after recovery/security design and testing.
7. **lilly-boot.** UEFI/BLS/UKI status and atomic deployment, /boot/efi support,
   secure boot, EFI variable handling and LPM kernel hooks. Keep systemd-boot and
   other bootloader options available regardless of selected init provider.

Every milestone must distinguish implemented behavior from future contracts.
There are no placeholder administrative commands that return success without
performing the requested operation.
