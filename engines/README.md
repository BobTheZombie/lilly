# Standalone Lilly device and login1 engines

These are source-built C engines, not wrappers that exec an installed udevd or
elogind daemon. The source manifests pin upstream commits; the small Lilly patch
series is applied before a Meson/Ninja build. Upstream notices and licenses are
retained. Lilly core/build integration is MIT; imported engine code remains under
its original LGPL/GPL component licenses. This is an integration/fork of existing
code, not a claim that the engine internals were written from scratch.

## Build

Install a native C toolchain, Meson, Ninja, Python with Jinja2, git, gperf,
pkg-config, patchelf and development files for libcap, libmount/libblkid,
libkmod, libacl, libaudit, libseccomp and Linux-PAM. Polkit and a system D-Bus
broker are runtime dependencies of the full session profile. The udev source
build supplies libudev headers/library to the login engine; external libudev
headers and an external device daemon are not required.

```sh
python3 tools/build_engines.py fetch --engine udev
python3 tools/build_engines.py fetch --engine logind
meson setup build-full -Dpam=enabled -Dnative_engines=true \
  -Dengine_sources="$PWD/build/engine-sources" -Dengine_jobs=4
meson compile -C build-full
meson test -C build-full --print-errorlogs
meson install -C build-full --destdir "$PWD/stage"
```

Fetching is explicit; build/install never fetch mutable branches. Source HEAD
must match the manifest. No checkout reset erases local changes. Build metadata
records patch hashes and the source diff hash; stage rejects changed patches or
source. Compiled objects remain available for incremental Ninja builds. Changing
engine sources manually requires invoking the build driver explicitly. Installation
requires a real staging DESTDIR and never runs the device or login daemons on
the build host. A private hardware DB is generated under that staging root.
Cross-building this engine integration is not currently supported.

The LPM recipe selects this full profile. Source caches live under SRCROOT and
remain subject to LPM build-state/resume handling. The bootstrap core can still
be built without native_engines; that profile retains the external provider
adapter and basic PAM session records.

## Payload

- /usr/lib/lilly/engines/lilly-udevd: netlink, worker scheduling, rule parser,
  database, node permissions, symlinks, builtin imports and event handling.
- /usr/bin/lilly-udevadm: real info/trigger/settle/control/monitor/test/verify tools.
- /usr/bin/lilly-hwdb: source-built hardware database compiler/query tool.
- Private libudev.so.1, helper programs, bundled rules, hwdb sources and cache.
- /usr/lib/lilly/engines/lilly-logind: standalone login1 D-Bus service.
- /usr/bin/lilly-loginctl and /usr/bin/lilly-inhibit: actual login1 clients.
- /usr/lib/security/pam_lilly_login.so: full login1 PAM client; creates sessions
  through D-Bus and holds/relinquishes the session FIFO descriptor.
- Private runtime libraries, source/patch provenance and original licenses.

/etc/udev and standard vendor rules remain recognized. Private bundled rules are
lowest priority; administrator and existing vendor files with the same basename
win. Private helpers are explicitly referenced by the bundled rules. The hwdb
compiler writes Lilly's private cache by default; administrator caches retain
priority. Systemd-specific vconsole/unit-trigger rules are not bundled. Lilly's
native engine does not create systemd device units or implement their job graph.
The device daemon owns its real control/netlink sockets; no dummy Unix socket
unit emulates that protocol.

## Selecting the native profile

Use one device daemon and one org.freedesktop.login1 owner. Native-engine builds
configure lilly-devices to use the private daemon and lilly-udevadm. Enable
udev-trigger.service and lilly-logind.service after selecting this provider.
LPM's other init/provider choices remain available; no /sbin/init link is changed.

D-Bus and Polkit policy files are staged under /usr/share/lilly/login1 for review.
An activation transaction must install the appropriate policies into the broker
and Polkit policy directories, respecting existing package ownership. Do not
blindly overwrite another provider's policies or run two login1 servers. No D-Bus
auto-activation file silently selects Lilly. The native unit starts it explicitly.

Add pam_lilly_login.so to the existing login/sshd/su/runuser session stacks once
Lilly is selected. Do not stack it with pam_systemd, pam_elogind or basic
pam_lilly.so. Authentication/account/password policy remains owned by the login
packages. Full sessions use the engine's standard /run/systemd session/user
compatibility records and per-user runtime mounts; lilly-loginctl is the matching
administration tool. Basic lilly-sessions records are a separate bootstrap mode.

PowerOff/Reboot/Halt route through authenticated servicectl after the login
engine's normal authorization/inhibitor checks, avoiding systemctl/PID1 coupling.
Sleep operations use the engine's native kernel sleep code. Hardware support,
firmware and policy determine which operations are available.

## Compatibility boundary

The baseline is the pinned udev 257.10 and elogind/login1 256 implementations.
This includes the actual rule language and login1 session/user/seat objects,
CreateSessionWithPIDFD, controller/device descriptor handling, inhibitors,
signals, policy checks, runtime directories and power/sleep machinery implemented
by those sources. It is not blanket compatibility with every later systemd
release. Elogind's documented differences remain: no systemd user-service
manager/resource-unit implementation, and no claim to homed or a full systemd
manager API. See sources.json and the upstream API documentation for the precise
versions. Do not label this development profile universally compatible or
production validated across arbitrary desktop/hardware combinations.

The disposable native-engine VM checks advanced rules (matching, GOTO, PROGRAM,
IMPORT, substitutions/list assignment, final permissions and RUN), private hwdb,
real login1 introspection, inhibitor FD lifetime, an unprivileged PAM session and
session properties, denied unprivileged power control, and an authorized D-Bus
PowerOff that reaches Lilly PID 1. Physical seat switching, compositor TakeDevice
handoff, suspend/hibernate and Polkit-agent interaction still need dedicated
hardware/desktop coverage; introspection alone does not validate them.
