# util-linux / Linux-PAM integration

Build with -Dpam=enabled and Linux-PAM development files. pam_lilly.so is a
session module, not an authentication replacement. It resolves an existing
account, creates/validates a private /run/user/UID directory, publishes a root-only
session record, and sets PAM environment XDG_RUNTIME_DIR and XDG_SESSION_ID.
Closing the PAM session or ending its handle removes that handle's record.
Concurrent sessions have independent random IDs; repeated opens on one handle
are idempotent. A client killed without closing/ending its handle can leave a
stale record until reboot; automatic crash reaping is not implemented. Runtime directories survive
logout, because another session or user service may still use them.

Review lilly-session.example and add its session line to the existing policies
used by util-linux login, su, su-l, runuser, runuser-l and sshd as appropriate.
Do not replace their auth/account/password stacks. No installer modifies these
files or removes pam_systemd/pam_elogind. Existing users may need a policy choice
before enabling this module. `required` may be chosen after local testing if a
session tracking failure must deny login; the example uses `optional`.

agetty calls the external /usr/bin/login explicitly. That program handles
passwords, PAM authentication/account checks, privilege changes and utmp/wtmp.
The Lilly module does not implement seats, logind D-Bus, session cgroups,
user-service managers, inhibitors, homed, automatic lingering or device ACLs.
It does not authorize device access. /run/lilly/sessions is root-only and
`lilly-sessions list` reads these records. No session/username is trusted as a
path component. No PAM configuration is installed into /etc automatically.
