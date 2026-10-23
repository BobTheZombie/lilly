# Vendored engine sources

These directories are source snapshots imported into Lilly so engine integration
work is reviewed and built in this repository. They are not submodules and are
not downloaded during package builds.

| Directory | Upstream | Imported commit | Version |
| --- | --- | --- | --- |
| `udev/` | https://github.com/systemd/systemd | `32ba9cee3c9cef33b7081768e8ff58b410a523da` | systemd 257.10 udev |
| `logind/` | https://github.com/elogind/elogind | `361db5752b16122fc21cf8c8cf188bcf45f9702e` | elogind 256 |

The upstream license and notice files are retained. Lilly adaptations are made
directly in these trees and must keep the affected upstream copyright headers.
Elogind's optional `pwx` migration-tools submodule is not part of its build and
is omitted; Lilly builds use the checked-in elogind source only.
When importing a later upstream revision, preserve the upstream commit in
`engines/sources.json` and update this table. Review the full source diff and
rerun the native-engine build and validation suite.
