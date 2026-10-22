# Device management and util-linux integration

Lilly's device path uses an actual udev provider: eudev or systemd-udevd and its
matching udevadm/libudev/rules/hwdb packages. Lilly does not implement a new rule
engine or claim compatibility with every recent systemd-udevd feature. The
selected provider implements netlink event validation, parallel event workers,
rule matching, device permissions/symlinks, persistent database, hwdb and modalias
module loading to the extent supported by that provider/version/build.

The C lilly-devices tool supplies daemon lifecycle and bounded administration.
Provider paths are fixed at Meson configuration, never taken from the runtime
PATH or an environment override. Defaults suit LPM's eudev layout:

```sh
meson setup build -Dpam=enabled \
  -Dudev_daemon=/usr/bin/udevd -Dudev_admin=/usr/bin/udevadm
```

For a systemd-udevd provider set udev_daemon=/usr/lib/systemd/systemd-udevd.
It runs in foreground under Lilly even when systemd is not PID 1. Keep the
provider's matching libudev, udevadm, rules, hwdb and builtins installed. Do not
install two packages that own the same provider files or run competing device
daemons on the same /run/udev database. libudev capabilities belong to the actual
provider package, not to a placeholder or the Lilly adapter.

Enable udev-trigger.service to select its readiness and daemon Requires closure:

```sh
servicectl enable udev-trigger.service
```

The sequence is filesystems -> udevd -> acknowledged control readiness ->
subsystem add events -> device add events -> queue settle. NetworkManager waits
for this coldplug barrier. `Type=simple` of the daemon alone cannot establish
readiness. An acknowledged reload is the common readiness command supported by
both providers; discovering a socket inode alone is insufficient.

lilly-devices --timeout 30 ready, coldplug, settle and reload use monotonic
budgets; failing subprocesses abort the sequence. Termination/timeouts kill and
reap the administration process group. Missing providers and failed trigger or
settle commands propagate errors. The daemon is exec'd in place so Lilly owns
its PID and termination directly. info, monitor, test, test-builtin and trigger
pass argv directly to udevadm without a shell. Raw forwarding commands retain
the provider's own authorization and timeout behavior.

The provider owns its control and netlink sockets. They are not Lilly Unix
.socket units: opening a second unrelated Unix socket cannot emulate its netlink
protocol. Provider-specific socket activation remains a provider feature.

## Login and storage units

console-getty.service uses tty1; getty.service uses ttyS0. Both run util-linux
agetty with an explicit /usr/bin/login. The login package owns authentication,
PAM policy and utmp/wtmp accounting. Enable only terminals actually available.
Existing filesystems.service invokes mount -a; swap.service invokes swapon --all
after mounts and device coldplug. These units are disabled until selected and
require a reviewed fstab and a correctly mounted/remounted real root.
This is not a complete fsck, encrypted-volume or shutdown-storage policy.

Build pam_lilly.so with -Dpam=enabled. See ../pam/README.md in the repository
(or the adjacent installed pam documentation) before adding the session module
to existing login/su/runuser/sshd policies. It creates runtime directories and
tracks concurrent PAM sessions; it does not replace authentication or logind.
No installer overwrites existing PAM configuration. LPM multi-init remains
available, and Lilly's session policy is an explicit administrator choice.

## Validation

The native test uses a fake executable only to check argv/order/failure/deadline
semantics. A separate QEMU test boots the actual device provider with Lilly PID 1,
creates a virtio disk, applies real rules, checks properties and symlinks,
reloads rules, removes the device and verifies cleanup. In the same guest actual
Linux-PAM tests multiple session handles, runtime directory ownership,
cleanup/symlink denial; actual util-linux runuser enters a nobody session with
the module. Host audit/socket restrictions are reported as skips; VM assertions
must pass independently.
