# Lillyboot: x86-64 UEFI loader and boot configuration

Build the real GNU-EFI firmware application with Meson/Ninja:

```sh
meson setup build -Defi=true
meson compile -C build
```

GNU-EFI headers/libraries and binutils are build dependencies. The result is
lillybootx64.efi, installed below /usr/lib/lilly/efi. It reads lilly-*.conf BLS
style entries from its own ESP, displays a firmware console menu, and starts an
x86-64 EFI-stub Linux kernel with its command line and initrd. There is one
initrd per entry, a maximum of 32 entries, and ASCII configuration text. This
initial implementation does not build UKIs or provide interactive cmdline edits.

The native C configuration tool defaults to the ESP mounted at /boot/efi:

```sh
lillyboot install --efi /usr/lib/lilly/efi/lillybootx64.efi
lillyboot generate --version 7.2.11-lpm2 \
  --kernel /usr/lib/modules/7.2.11-lpm2/vmlinuz \
  --initrd /boot/initrd-7.2.11-lpm2.img \
  --cmdline /etc/kernel/cmdline --set-default --timeout 5
lillyboot status
```

These are administrative examples, not actions performed by package install.
Live writes require a mounted FAT ESP. `--root /target` supports offline image
creation, with all source paths interpreted inside that root. Symlink traversal,
invalid kernel PE images and multiline command lines are rejected. The supplied
cmdline must include the correct root/storage parameters. Lillyboot does not
implicitly choose init: bootloader selection remains independent of PID 1.

Private firmware goes to EFI/Lilly/lillybootx64.efi. Firmware registration is
explicit and external; this tool does not modify EFI variables. Optional
`install --efi ... --fallback` installs EFI/BOOT/BOOTX64.EFI only when absent or
when its previous contents match Lilly's prior private loader. Another loader's
fallback file is preserved. Firmware LoadImage verification remains in effect;
Secure Boot signing, key enrollment and hardware verification are not included.

Each generated entry references a new immutable kernel/initrd generation.
Assets are copied first and the entry is published atomically last. Repeating a
version preserves previous assets. No old boot generations are automatically
deleted. Defaults and timeout live in loader/lilly.conf, leaving other loaders'
configuration intact. Keep a tested recovery entry and its assets.

`lillyboot update --set-default` enumerates installed /usr/lib/modules versions,
matching vmlinuz with /boot/initrd-VERSION.img or /boot/initramfs-VERSION.img,
and chooses the highest version processed. It does not build the initramfs.

The libalpm-compatible 95-lillyboot.hook is opt-in: create /etc/lilly/bootloader
containing exactly `lillyboot`. Its helper requires mount/findmnt, mounts the ESP
using fstab if necessary, updates the private loader, then generates entries.
The initramfs generation hook must run before it. It never changes /sbin/init or
other providers. Updating a previously installed fallback loader remains an
explicit `install --efi ... --fallback` operation.

An OVMF/QEMU test boots this actual EFI binary, generated entry, EFI-stub kernel
and initramfs through to Lilly PID 1 and clean poweroff. This does not establish
Secure Boot or physical hardware compatibility.
