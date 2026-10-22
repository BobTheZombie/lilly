# Milestones

1. **Current: C service-supervision foundation.** Parser, plan, foreground
   supervisor, bounded restart, shutdown and executable integration tests.
2. **Lifecycle completion.** cgroup v2, runtime Requires failure propagation,
   reverse dependency stop ordering, timeouts, readiness, persistent status,
   authenticated local control socket and lillyctl. Add parallel startup tests.
3. **Bootable PID 1 in QEMU.** Early mounts, console, rescue, root handoff,
   shutdown/reboot, filesystem sync/unmount, initramfs integration and CI boot
   tests. Only then add a separate LPM init-provider activation package.
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
