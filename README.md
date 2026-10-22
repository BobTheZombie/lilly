# LILLY

**Linux Init, Login & Lifecycle System** — a modular Linux system manager in C,
started for LPM Linux, with Linux interfaces intended for other distributions.

**0.4.0 implements experimental PID 1 supervision, service administration,
Unix socket activation, logging and a real x86-64 UEFI bootloader.** Meson/Ninja
build all programs. Installing the package does not change the active init or
bootloader and does not enable services. LPM multi-init choices remain available.

- Dependency ordered parallel services, timeouts, bounded restart policies,
  per-service CPU policies, cgroup descendant cleanup and authenticated control.
- `servicectl` enable/disable/status/start/stop/restart, including offline roots.
- Native vendor units for filesystem, device, D-Bus, networking, SSH and getty
  services; existing external daemons remain their providers.
- Unix socket activation, descriptor handoff, service identities and /run directories.
- Bounded udev readiness/coldplug/control integration with an actual provider.
- Linux-PAM session module and util-linux console login/swap units.
- `lilly-logd`, a credential-aware /dev/log datagram logger.
- `lillyboot`, atomic ESP deployment/configuration and an optional GNU-EFI loader.
- Disposable PID 1 and OVMF firmware boot tests. Experimental, not a complete
  systemd/logind/homed replacement or a hard real-time guarantee.

## Build and try

Linux, a C17 compiler, Meson (>=1.3) and Ninja are required. Python 3 is
used by the build system and tests; it is not a runtime dependency of Lilly.
The executable links only against libc. Service commands use explicit argv
parsing, with no shell expansion unless an administrator explicitly runs a shell.

```sh
meson setup build --prefix=/usr
meson compile -C build
meson test -C build --print-errorlogs
build/lilly verify examples/system
build/lilly plan examples/system
# Small foreground example (no host init change):
mkdir -p /tmp/lilly-demo
cp examples/hello.service /tmp/lilly-demo/
build/lilly run /tmp/lilly-demo
meson install -C build --destdir "$PWD/stage"
```

```text
lilly --help
lilly --version
lilly verify UNIT_DIRECTORY [SERVICE]
lilly plan UNIT_DIRECTORY [SERVICE]
lilly run UNIT_DIRECTORY [SERVICE]
lilly init [UNIT_DIRECTORY]                 # PID 1 only
lilly ctl SOCKET {status|stop|poweroff|reboot}
```

PID 1 loads /etc/lilly/system overrides and /usr/lib/lilly/system vendor units,
selecting /etc/lilly/enabled markers and their Requires closure. The legacy
/etc/lilly/services directory, when present, retains select-all behavior.
Explicit `lilly run DIRECTORY` also selects that directory's units. Simple
services are considered started after exec, not application readiness.

Read [device/PAM integration](docs/DEVICES.md), [service administration](docs/SERVICES.md), [Lillyboot](docs/LILLYBOOT.md),
[PID 1 setup](docs/PID1.md), [scheduling](docs/SCHEDULING.md),
[compatibility](docs/COMPATIBILITY.md) and [validation](docs/VALIDATION.md).
