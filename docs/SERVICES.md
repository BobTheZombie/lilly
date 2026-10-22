# Native services and administration

PID 1 reads administrator units from /etc/lilly/system before vendor units from
/usr/lib/lilly/system. Files in /etc/lilly/enabled select the initial Requires
closure. Units are installed disabled. After orders selected units; it does not
select them. Requires selects dependencies but does not imply ordering.

```sh
servicectl list-unit-files
servicectl enable filesystems.service
servicectl enable udevd.service
```

Commands accept one unit per invocation: run enable separately for each service.
Use `servicectl enable dbus`, `servicectl --now enable dbus`,
`servicectl --now disable dbus`, `servicectl start dbus`, `servicectl stop dbus`,
`servicectl restart dbus`, `servicectl status`, and `servicectl is-enabled dbus`.
Enable/disable persist regular marker files; disabling alone leaves a running
service running. Runtime actions require an authorized local peer. Offline
`servicectl --root /target enable dbus` edits only that root. Runtime actions
with --root require an explicit --socket to prevent accidental host control.

The catalog is fixed at manager startup: adding or editing unit files requires
a manager restart/reboot. There is no daemon-reload, mask, target, preset or
systemd D-Bus API. A disabled unit can still be selected as a hard dependency.
Use `lilly verify-system /target` before booting an offline root.

Services support simple/oneshot execution, Requires/After, restart limits,
timeouts and the CPU policies described in SCHEDULING.md. User and Group resolve
existing NSS accounts. RuntimeDirectory accepts one safe component below /run;
RuntimeDirectoryMode defaults to 0755. Account creation is separate. Directory
ownership is assigned before dropping all user/group IDs and setting no-new-privs.
No general sandbox, capability policy, ExecStop or notify readiness is provided.
Simple readiness means exec succeeded, not that a daemon has finished startup.

Socket units support one absolute Unix ListenStream or ListenDatagram,
SocketMode, Service and Accept=no. One listener per service is supported.
The activated service receives descriptor 3 and LISTEN_PID/LISTEN_FDS=1/
LISTEN_FDNAMES. Existing socket paths are never overwritten. Stopping a socket
also stops its activated service. TCP, Accept=yes and multiple listener handoff
are not supported.

Vendor units cover fstab mounts, eudev, device coldplug/readiness, D-Bus, NetworkManager,
OpenSSH and a serial getty. Their actual binaries, accounts, PAM configuration,
SSH keys, fstab and networking configuration must be supplied by their packages
and reviewed locally. dbus.service uses the LPM account dbus. These are native
units, not transparent loading of systemd units. No units are auto-enabled.

lilly-logd is the included C logging daemon. lilly-logd.socket receives Unix
/dev/log datagrams and starts it on demand. Records include peer uid/pid and
escaped control characters in /var/log/lilly.log. SIGHUP reopens the output for
external log rotation. This is not the journald API and does not implement
journal queries, automatic rotation or persistent rate quotas.

Device startup now passes an acknowledged control readiness barrier and a
subsystem/device trigger plus queue settle before dependent networking starts.
See DEVICES.md for the configured provider contract. console-getty.service
adds a tty1 login through util-linux; swap.service activates fstab swap.
pam_lilly is available for the existing login/su/runuser/sshd PAM session stacks;
no authentication policy is overwritten or auto-enabled.

The standalone-engine profile provides lilly-logind.service and matching
lilly-console-getty/lilly-serial-getty units. Select one getty per terminal.
The matching full PAM client is pam_lilly_login.so, distinct from bootstrap
pam_lilly.so. See engines/README.md for source builds and policy activation.
servicectl halt requests an actual kernel halt, distinct from poweroff/reboot.
