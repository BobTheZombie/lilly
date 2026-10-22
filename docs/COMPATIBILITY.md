# Compatibility contract

Lilly is additive to LPM's existing multi-init choices. Systemd remains a first
class option. No package should claim `systemd`, `libsystemd`, `libudev`, logind,
or a protocol capability until it implements and tests that contract.

| Interface | Current state | Intended contract |
| --- | --- | --- |
| Service units | Small explicit subset | Document each supported directive; reject unknown sections and directives |
| PID 1 | Experimental QEMU-tested boot, persistence, shutdown and emergency mode | Reap all children; survive failures; early mounts, rescue, shutdown and switch-root tested in a VM |
| cgroup v2 | Per-service descendant ownership/kill in PID 1; resource controllers pending | Per-service lifecycle ownership and cleanup, resource controls |
| Notify/socket activation | Unix one-listener socket activation implemented; notify planned | Documented sd_notify and LISTEN_FDS interoperability, without requiring libsystemd |
| sysusers.d | Planned lilly-sysusers | Stable IDs, locking, atomic account updates, administrator precedence, offline root |
| tmpfiles.d | Planned lilly-tmpfiles | Explicit supported types, no symlink traversal, root confinement, safe cleanup semantics |
| PAM/login/session | Source-built login1/PAM client in full profile; basic records in bootstrap profile | PAM-backed login, seat/session ownership, shutdown inhibitors; use elogind until equivalent coverage exists |
| NSS/userdb | Planned lilly-userdb | NSS interoperability, documented user/group JSON and Varlink protocols |
| Managed homes | Planned lilly-homed | PAM activation, account records, encryption/recovery, safe unmounting and credential handling |
| UEFI/boot | x86-64 GNU-EFI Lillyboot and atomic generation implemented; UKI/signing/NVRAM tools pending | BLS Type 1 entries/UKIs, ESP at /boot/efi, atomic updates, Secure Boot preservation and explicit EFI variable changes |
| udev/device management | Standalone source-built 257.10 engine in full profile; external adapter in bootstrap | Reuse eudev or another compatible provider; no fake libudev capability |
| D-Bus | External | dbus-broker/dbus-daemon; logind and other desktop APIs require actual implementations |
| Power management | External | Retain UPower and power-profiles-daemon integration; do not duplicate their policy |
| LPM hooks | Packaging contract | Existing libalpm-compatible trigger syntax; vendor /usr/share/liblpm/hooks, helpers /usr/libexec/lpm/hooks, admin /etc/lpm/hooks |

The package installs lilly, servicectl, lillyboot, lilly-logd, disabled native
units and an opt-in LPM boot hook. Optional EFI builds install Lilly's private
firmware artifact. It does not own /sbin/init, systemctl, bootctl or other init
providers' files. Activation and firmware registration remain explicit.

User, Group and a single RuntimeDirectory are supported. Existing accounts are
required. Unsupported Environment, ExecStop, notify/forking, sandbox directives,
targets, drop-ins, specifiers and command prefixes fail validation. Native
catalog overrides and enablement are described in SERVICES.md; editing that
catalog requires a manager restart/reboot. After does not pull dependencies in.
CPU policies are not a security sandbox. Full desktop APIs remain future work.

0.4 adds lilly-devices and pam_lilly. Device rules, hwdb, netlink workers and
libudev remain the actual provider's implementation, not a new Lilly engine.
The session module does not implement logind, seats or device ACL assignment.
See DEVICES.md and the shipped pam documentation for activation boundaries.

0.5 source-builds udev 257.10 and elogind/login1 256 into private Lilly binaries
with retained upstream licenses and recorded provenance. Runtime external udevd
or elogind executables are unnecessary in that profile. See engines/README.md
for exact API/version boundaries, activation and outstanding hardware validation.
This does not implement the systemd user-service manager or every later login1
API addition. Native PowerOff/Reboot/Halt goes through authenticated servicectl.
