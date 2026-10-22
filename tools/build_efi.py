#!/usr/bin/env python3
"""Meson custom target: freestanding C -> GNU-EFI ELF -> PE/COFF. No Make."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser()
for key in ['source','output','cc','ld','objcopy','include','lib']: p.add_argument('--'+key,required=True)
a=p.parse_args()
include=Path(a.include);lib=Path(a.lib)
with tempfile.TemporaryDirectory() as work:
    obj=Path(work)/'loader.o';elf=Path(work)/'loader.so'
    subprocess.run([a.cc,'-std=c17','-O2','-Wall','-Wextra','-Werror',
        '-fpic','-ffreestanding','-fno-stack-protector','-fno-stack-check','-fshort-wchar','-mno-red-zone',
        '-DEFI_FUNCTION_WRAPPER','-I'+str(include),'-I'+str(include/'x86_64'),
        '-c',a.source,'-o',str(obj)],check=True)
    subprocess.run([a.ld,'-nostdlib','-znocombreloc','-T',str(lib/'elf_x86_64_efi.lds'),
        '-shared','-Bsymbolic',str(lib/'crt0-efi-x86_64.o'),str(obj),
        '-L'+str(lib),'-lefi','-lgnuefi','-o',str(elf)],check=True)
    subprocess.run([a.objcopy,'-j','.text','-j','.sdata','-j','.data','-j','.rodata',
        '-j','.dynamic','-j','.dynsym','-j','.rel','-j','.rela','-j','.reloc',
        '--target=efi-app-x86_64',str(elf),a.output],check=True)
