# Compatibility contract

Lilly is additive to LPM's existing multi-init choices. Systemd remains a first
class option. No package should claim `systemd`, `libsystemd`, `libudev`, logind,
or a protocol capability until it implements and tests that contract.

| Interface | Initial state | Intended contract |
| --- | --- | --- |
| Service units | Small explicit subset | Document each supported directive; reject unknown sections and directives |
| PID 1 | Refused | Reap all children; survive failures; early mounts, rescue, shutdown and switch-root tested in a VM |
| cgroup v2 | Planned | Per-service lifecycle ownership and cleanup, resource controls |
| Notify/socket activation | Planned | Documented sd_notify and LISTEN_FDS interoperability, without requiring libsystemd |
| sysusers.d | Planned lilly-sysusers | Stable IDs, locking, atomic account updates, administrator precedence, offline root |
| tmpfiles.d | Planned lilly-tmpfiles | Explicit supported types, no symlink traversal, root confinement, safe cleanup semantics |
| PAM/login/session | Planned | PAM-backed login, seat/session ownership, shutdown inhibitors; use elogind until equivalent coverage exists |
| NSS/userdb | Planned lilly-userdb | NSS interoperability, documented user/group JSON and Varlink protocols |
| Managed homes | Planned lilly-homed | PAM activation, account records, encryption/recovery, safe unmounting and credential handling |
| UEFI/boot | Planned lilly-boot | BLS Type 1 entries/UKIs, ESP at /boot/efi, atomic updates, Secure Boot preservation and explicit EFI variable changes |
| udev/device management | External | Reuse eudev or another compatible provider; no fake libudev capability |
| D-Bus | External | dbus-broker/dbus-daemon; logind and other desktop APIs require actual implementations |
| Power management | External | Retain UPower and power-profiles-daemon integration; do not duplicate their policy |
| LPM hooks | Packaging contract | Existing libalpm-compatible trigger syntax; vendor /usr/share/liblpm/hooks, helpers /usr/libexec/lpm/hooks, admin /etc/lpm/hooks |

The initial development package installs `/usr/bin/lilly`, documentation and an
example. It does not own `/sbin/init`, `systemctl`, `bootctl`, reboot/halt links,
systemd files, PAM policy, sysusers/tmpfiles files or transaction hook names.
A future separate activation package must make the init-provider switch explicit
and reversible. Bootloader choice must remain independent of PID 1 choice.

The current parser rejects User, Group, Environment, ExecStop, forking/notify
services, sandbox directives, target units, includes, drop-ins, specifiers,
continuations and command prefixes. Silently ignoring these could run services
with incorrect security settings. No existing vendor directory is read by
default. After of an absent/unselected unit has no ordering effect. Hard
requirements must exist. Runtime Requires propagation beyond startup is not yet
implemented; do not use this supervisor for a production dependency graph.
