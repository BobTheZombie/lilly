project('lilly', 'c',
  version: '0.2.0',
  meson_version: '>=1.3.0',
  license: 'MIT',
  default_options: ['c_std=c17', 'warning_level=3', 'werror=true', 'buildtype=debugoptimized'])

if host_machine.system() != 'linux'
  error('Lilly requires Linux APIs')
endif
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments('-Wformat=2', '-Wshadow'), language: 'c')
sources = files('src/lilly.c', 'src/platform.c')
lilly = executable('lilly', sources, install: true)

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
  vm_init = executable('lilly-init-static', sources, link_args: ['-static'], install: false)
  probe = executable('lilly-probe-static', 'tests/probe.c', link_args: ['-static'], install: false)
  test('pid1-vm', python,
    args: [files('tests/vm_test.py'),
      '--kernel', get_option('vm_kernel'), '--busybox', get_option('vm_busybox'),
      '--qemu', qemu.full_path(), '--lilly', vm_init.full_path(), '--probe', probe.full_path(),
      '--log-dir', meson.current_build_dir() / 'vm-logs'],
    depends: [vm_init, probe], suite: 'vm', timeout: 360, is_parallel: false)
endif
