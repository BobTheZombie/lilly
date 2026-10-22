project('lilly', 'c',
  version: '0.3.0',
  meson_version: '>=1.3.0',
  license: 'MIT',
  default_options: ['prefix=/usr', 'libexecdir=libexec', 'libdir=lib', 'sysconfdir=/etc', 'localstatedir=/var', 'c_std=c17', 'warning_level=3', 'werror=true', 'buildtype=debugoptimized'])

if host_machine.system() != 'linux'
  error('Lilly requires Linux APIs')
endif
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments('-Wformat=2', '-Wshadow'), language: 'c')
sources = files('src/lilly.c', 'src/platform.c', 'src/fsutil.c')
lilly = executable('lilly', sources, install: true)
logd = executable('lilly-logd', 'src/logd.c', install: true)
lillyboot = executable('lillyboot', ['src/lillyboot.c', 'src/fsutil.c'], install: true)
servicectl = executable('servicectl', ['src/servicectl.c', 'src/fsutil.c'], install: true)

install_subdir('units', install_dir: get_option('libdir') / 'lilly' / 'system', strip_directory: true)
install_emptydir(get_option('sysconfdir') / 'lilly' / 'system')
install_emptydir(get_option('sysconfdir') / 'lilly' / 'enabled')
install_data('hooks/95-lillyboot.hook', install_dir: get_option('datadir') / 'liblpm' / 'hooks')
install_data('hooks/boot-update', install_dir: get_option('libexecdir') / 'lilly', install_mode: 'rwxr-xr-x')

docdir = get_option('datadir') / 'doc' / 'lilly'
install_data('README.md', install_dir: docdir)
install_subdir('docs', install_dir: docdir)
install_subdir('examples', install_dir: docdir)
install_data('LICENSE', install_dir: get_option('datadir') / 'licenses' / 'lilly')

if get_option('tests') or get_option('vm_tests')
  python = import('python').find_installation('python3')
endif
if get_option('tests')
  if meson.is_cross_build()
    error('Use -Dtests=false for cross builds; run lifecycle tests on the target')
  endif
  test('supervisor', python,
    args: files('tests/test_supervisor.py'),
    env: {'LILLY_TEST_BINARY': lilly.full_path()},
    depends: lilly, suite: 'unit', timeout: 45)
endif
if get_option('tests')
  test('admin-offline', python, args: files('tests/test_admin.py'),
    env: {'LILLY_TEST_SERVICECTL': servicectl.full_path(), 'LILLY_TEST_BOOT': lillyboot.full_path()},
    depends: [servicectl, lillyboot], suite: 'unit', timeout: 30)
endif
if get_option('vm_tests')
  if host_machine.cpu_family() != 'x86_64' or meson.is_cross_build()
    error('The current QEMU boot harness requires native x86-64 builds')
  endif
  if get_option('b_sanitize') != 'none'
    error('Use a separate unsanitized build directory for static VM binaries')
  endif
  if not cc.links('int main(void) { return 0; }', args: ['-static'], name: 'static libc')
    error('VM tests require a static libc development toolchain')
  endif
  if get_option('vm_kernel') == '' or get_option('vm_busybox') == ''
    error('Set -Dvm_kernel=/path/to/vmlinuz -Dvm_busybox=/path/to/static/busybox')
  endif
  qemu = find_program(get_option('vm_qemu'))
  vm_init = executable('lilly-init-static', sources, link_args: ['-static', '-Wl,--no-fatal-warnings'], install: false)
  vm_svc = executable('servicectl-static', ['src/servicectl.c', 'src/fsutil.c'], link_args: ['-static'], install: false)
  vm_log = executable('lilly-logd-static', 'src/logd.c', link_args: ['-static'], install: false)
  probe = executable('lilly-probe-static', 'tests/probe.c', link_args: ['-static'], install: false)
  test('pid1-vm', python,
    args: [files('tests/vm_test.py'),
      '--kernel', get_option('vm_kernel'), '--busybox', get_option('vm_busybox'),
      '--qemu', qemu.full_path(), '--lilly', vm_init.full_path(), '--probe', probe.full_path(),
      '--servicectl', vm_svc.full_path(), '--logd', vm_log.full_path(),
      '--log-dir', meson.current_build_dir() / 'vm-logs'],
    depends: [vm_init, probe, vm_svc, vm_log], suite: 'vm', timeout: 360, is_parallel: false)
endif

if get_option('efi')
  if host_machine.cpu_family() != 'x86_64' or meson.is_cross_build()
    error('EFI loader currently supports native x86-64 toolchains')
  endif
  efi_python = import('python').find_installation('python3')
  linker = find_program('ld')
  objcopy = find_program('objcopy')
  efi_loader = custom_target('lillyboot-efi',
    input: 'efi/main.c', output: 'lillybootx64.efi',
    command: [efi_python, files('tools/build_efi.py'), '--source', '@INPUT@', '--output', '@OUTPUT@',
      '--cc', cc.cmd_array()[0], '--ld', linker.full_path(), '--objcopy', objcopy.full_path(),
      '--include', get_option('efi_include'), '--lib', get_option('efi_lib')],
    install: true, install_dir: get_option('libdir') / 'lilly' / 'efi')
endif

if get_option('efi_vm_tests')
  if not get_option('efi') or not get_option('vm_tests')
    error('EFI VM tests require -Defi=true -Dvm_tests=true')
  endif
  if get_option('ovmf_code') == '' or get_option('ovmf_vars') == ''
    error('Set -Dovmf_code=... -Dovmf_vars=... for UEFI boot tests')
  endif
  efi_qemu = find_program(get_option('efi_qemu'))
  test('uefi-boot', python,
    args: [files('tests/efi_test.py'),
      '--kernel', get_option('vm_kernel'), '--busybox', get_option('vm_busybox'),
      '--qemu', efi_qemu.full_path(), '--lilly', vm_init.full_path(), '--probe', probe.full_path(),
      '--servicectl', vm_svc.full_path(), '--logd', vm_log.full_path(),
      '--loader', efi_loader, '--boot-tool', lillyboot.full_path(),
      '--ovmf-code', get_option('ovmf_code'), '--ovmf-vars', get_option('ovmf_vars'),
      '--log-dir', meson.current_build_dir() / 'vm-logs'],
    depends: [vm_init, probe, vm_svc, vm_log, efi_loader, lillyboot],
    suite: 'vm', timeout: 150, is_parallel: false)
endif
