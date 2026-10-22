# Milestones

1. **Implemented: C service-supervision foundation.** Strict parser, dependency
   plan, parallel activation, bounded restarts, timeouts and runtime Requires
   failure propagation.
2. **Implemented: experimental PID 1 and CPU policy controls.** Early mounts,
   console, cgroup v2, reaping, persistent event loop, emergency mode, authenticated
   local control and shutdown/reboot. QEMU checks actual RR/affinity/reset-on-fork,
   runaway RT budget, escaped-child cleanup, idle and broken-config persistence.
3. **Implemented: service control, Unix socket activation and bootloader.**
   servicectl enable/disable/start/stop/restart, existing-account identities,
   runtime directories, credential-aware logging, native external-daemon units,
   atomic boot generation and a real QEMU/OVMF-tested x86-64 EFI loader.
   Next: notify readiness, live catalog reload, resource controllers, broader
   sandbox policy and real-root/storage integration tests.
4. **Implemented integration: devices and PAM sessions.** Bounded provider
   lifecycle/readiness/coldplug, actual rule/reload/removal VM tests, private runtime
   directories and concurrent PAM session tracking, util-linux runuser/console
   login integration. A new udev rule engine, logind/seats and user managers are
   not implemented.
5. **lilly-sysusers and lilly-tmpfiles.** Implement documented compatible subsets,
   offline root, precedence, locking, transactional writes and hostile path tests.
6. **Desktop compatibility.** Notify/socket activation, D-Bus service lifecycle,
   PAM login integration, elogind interoperability, UPower and
   power-profiles-daemon. Reuse device/network daemons rather than invent shims.
7. **lilly-userdb and lilly-homed.** NSS, Varlink, JSON records and PAM contracts;
   encrypted managed homes only after recovery/security design and testing.
8. **Further Lillyboot work.** UKIs, signing workflow, explicit EFI variable
   handling, hardware coverage and generation cleanup. Keep systemd-boot and
   other bootloader options available regardless of selected init provider.

Every milestone must distinguish implemented behavior from future contracts.
There are no placeholder administrative commands that return success without
performing the requested operation.
