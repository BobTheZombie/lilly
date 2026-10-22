# LILLY

**Linux Init, Login & Lifecycle System** — a modular Linux system manager in C,
started for LPM Linux and intended to work on other Linux distributions.

Version 0.1.0-dev is the first development milestone: a real service parser,
dependency planner and foreground process supervisor. **It is not yet a bootable
PID 1 and must not replace `/sbin/init`.** `lilly run` explicitly refuses PID 1.
There is no claim of systemd, logind, homed or bootctl drop-in compatibility.

LPM supports multiple init systems. Lilly is another selectable provider;
installing this package does not change the active init, systemd-boot, or hooks.

## Build and try

Linux, a C17 compiler and Make are required; Python 3 is used only for tests.
The executable links only against libc. No shell interprets service commands.

```sh
make
make check
build/lilly verify examples
build/lilly plan examples
build/lilly run examples
make DESTDIR="$PWD/stage" PREFIX=/usr install
```

Supported commands:

```text
lilly --help
lilly --version
lilly verify UNIT_DIRECTORY [SERVICE]
lilly plan UNIT_DIRECTORY [SERVICE]
lilly run UNIT_DIRECTORY [SERVICE]
```

Without SERVICE, every `.service` in the explicitly supplied directory is
selected. With SERVICE, only that unit and its transitive `Requires` closure
are selected. `After` orders selected units; it does not pull units in.
`Requires` does not imply `After`. This is a deliberately small, documented
subset of familiar unit syntax, not an importer for arbitrary systemd units.

Service files are trusted administrator code. `run` executes services with the
calling user's credentials and inherited environment, cwd and output streams.
Use an ordinary account for development. Do not feed untrusted directories to
root. Only foreground services are supported; daemonization and session/process
group escapes are outside the current lifecycle contract.

## Implemented

- Strict parsing of `[Unit]` Description, Requires and After, and `[Service]`
  Type=simple/oneshot, ExecStart and Restart=no/on-failure.
- Deterministic dependency plans, missing hard dependency errors and cycle checks.
- Absolute executable paths and a quoted argv tokenizer; no shell expansion.
- Fork/exec error reporting through a close-on-exec pipe.
- Linux signalfd/poll event loop, child reaping and subreaper registration.
- Oneshot completion ordering and foreground service supervision.
- Five starts maximum per invocation for on-failure services, 250ms retry delay.
- SIGTERM/SIGINT shutdown: reverse-order process group TERM, KILL after 3 seconds.
- LPM development package recipe, staged installation and CI.

`Type=simple` means exec started, **not application readiness**. There is no
notify protocol, socket activation, control socket, reload or service CLI yet.
The supervisor exits once selected processes have completed and returns failure
if a selected service failed. Output stays on the inherited stdout/stderr.

Read [compatibility](docs/COMPATIBILITY.md), [architecture](docs/ARCHITECTURE.md)
and the [implementation roadmap](docs/ROADMAP.md) before integration work.
