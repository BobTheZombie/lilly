#!/usr/bin/env python3
"""Build Lilly's in-tree udev and logind source snapshots with Meson/Ninja.
Build and stage never fetch sources or install to host /.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
SOURCES=json.loads((ROOT/'engines/sources.json').read_text())
PRIVATE='/usr/lib/lilly/engines'

def command(args,**kw):
    print('+',' '.join(map(str,args)),flush=True)
    return subprocess.run(list(map(str,args)),check=True,**kw)

def source(args):
    return args.sources/args.engine

def tree_hash(path):
    digest=hashlib.sha256()
    for root,dirs,files in os.walk(path,followlinks=False):
        dirs.sort();files.sort()
        base=Path(root)
        for name in dirs+files:
            item=base/name
            rel=item.relative_to(path).as_posix()
            digest.update(rel.encode()+b'\0')
            if item.is_symlink():
                digest.update(b'link\0'+os.readlink(item).encode()+b'\0')
            elif item.is_file():
                digest.update(b'file\0')
                with item.open('rb') as stream:
                    for chunk in iter(lambda:stream.read(1024*1024),b''):
                        digest.update(chunk)
                digest.update(b'\0')
    return digest.hexdigest()

def verify(args):
    path=source(args);spec=SOURCES[args.engine]
    if not (path/'meson.build').is_file():
        raise RuntimeError('Missing vendored source tree: '+str(path))
    return path

def prepare(args):
    return verify(args)

def options(engine):
    common=['--prefix=/usr','--libdir=lib','--sysconfdir=/etc','--localstatedir=/var',
            '--buildtype=release','--wrap-mode=nodownload','-Dmode=release','-Dauto_features=disabled',
            '-Dman=disabled','-Dtests=false','-Dacl=enabled','-Daudit=enabled','-Dversion-tag=Lilly-0.5']
    if engine=='udev':
        return common+['-Dhtml=disabled','-Dstandalone-binaries=true','-Dlink-udev-shared=false',
                       '-Dhwdb=true','-Dkmod=enabled','-Dblkid=enabled','-Dseccomp=enabled']
    return common+['--libexecdir='+PRIVATE,'-Dpolkit=enabled','-Dpam=enabled',
                   '-Dutmp=true','-Dcgroup-controller=lilly','-Dpamconfdir=no',
                   '-Dsshdconfdir=no','-Dshellprofiledir=no']

def build(args):
    path=prepare(args);directory=(args.build/args.engine).resolve()
    directory.mkdir(parents=True,exist_ok=True)
    setup=['meson','setup']
    if (directory/'meson-private/coredata.dat').exists():setup+=['--reconfigure','--clearcache']
    env=dict(os.environ)
    if args.engine=='logind':
        udev=(args.build/'udev').resolve()
        if not (udev/'libudev.so.1.7.10').is_file():raise RuntimeError('Build the standalone udev engine first')
        pc=directory/'lilly-pkgconfig';pc.mkdir(exist_ok=True)
        headers=(args.sources/'udev/src/libudev').resolve()
        (pc/'libudev.pc').write_text(f'Name: libudev\nDescription: Lilly source-built libudev\nVersion: 257\nLibs: -L{udev} -ludev\nCflags: -I{headers}\n')
        env['PKG_CONFIG_PATH']=str(pc)+os.pathsep+env.get('PKG_CONFIG_PATH','')
    command(setup+[directory,path]+options(args.engine),env=env)
    targets=(['udevadm','libudev.so.1.7.10','systemd-hwdb','ata_id','cdrom_id','scsi_id','v4l_id','mtd_probe','fido_id','iocost','dmi_memory_id']
             if args.engine=='udev' else ['elogind','loginctl','elogind-inhibit','elogind-uaccess-command','pam_elogind.so','libelogind.so.0.40.0'])
    installed=json.loads(subprocess.check_output(['meson','introspect','--installed',str(directory)],text=True))
    for src,dest in installed.items():
        if isinstance(dest,str) and (dest.startswith('/usr/lib/udev/') or dest.startswith('/usr/share/polkit-1/actions/') or dest.startswith('/usr/share/dbus-1/system.d/')):
            try:target=Path(src).relative_to(directory)
            except ValueError:continue
            targets.append(str(target))
    command(['ninja','-C',directory,'-j',str(args.jobs)]+targets,env=env)
    state={'engine':args.engine,'source':SOURCES[args.engine],
           'source_tree_sha256':tree_hash(path)}
    (directory/'lilly-engine.json').write_text(json.dumps(state,indent=2)+'\n')

def stage(args):
    verify(args)
    if args.destdir.resolve()==Path('/'):raise RuntimeError('A staging DESTDIR is required; host installation is forbidden')
    directory=(args.build/args.engine).resolve();state=directory/'lilly-engine.json'
    if not state.exists():raise RuntimeError('Build the engine before staging it')
    if json.loads(state.read_text())['source_tree_sha256']!=tree_hash(source(args)):
        raise RuntimeError('Vendored source changed since build')
    installed=json.loads(subprocess.check_output(['meson','introspect','--installed',str(directory)],text=True))
    copied=[]
    def destination(relative):
        current=args.destdir
        for part in Path(relative.lstrip('/')).parts[:-1]:
            if part in ('.','..'):raise RuntimeError('Invalid staging component')
            current=current/part
            if current.is_symlink():raise RuntimeError('Refusing staged parent symlink: '+str(current))
        return args.destdir/relative.lstrip('/')
    def copy(src,dest,elf=False):
        src=Path(src)
        if not src.is_file():raise RuntimeError('Missing built artifact: '+str(src))
        dest=destination(dest);dest.parent.mkdir(parents=True,exist_ok=True)
        if dest.is_symlink():raise RuntimeError('Refusing staged symlink: '+str(dest))
        shutil.copy2(src,dest)
        if elf:command(['patchelf','--set-rpath',PRIVATE,str(dest)])
        copied.append(str(dest.relative_to(args.destdir)))
    if args.engine=='udev':
        copy(directory/'udevadm',PRIVATE+'/lilly-udevd',True)
        copy(directory/'udevadm','/usr/bin/lilly-udevadm',True)
        copy(directory/'systemd-hwdb','/usr/bin/lilly-hwdb',True)
        copy(directory/'libudev.so.1.7.10',PRIVATE+'/libudev.so.1',True)
        for name in ['ata_id','cdrom_id','scsi_id','v4l_id','mtd_probe','fido_id','iocost','dmi_memory_id']:
            # The engine uses the standard helper lookup directory. Keep helpers
            # private and inject an explicit helper path into bundled rules below.
            copy(directory/name,PRIVATE+'/udev/'+name,True)
        for src,dest in installed.items():
            if isinstance(dest,str) and (dest.startswith('/usr/lib/udev/rules.d/') or dest.startswith('/usr/lib/udev/hwdb.d/')):
                if not Path(src).is_file():raise RuntimeError('Required device data missing: '+src)
                if Path(src).is_file():
                    if Path(dest).name in ('90-vconsole.rules','99-systemd.rules'):continue
                    private=dest.replace('/usr/lib/udev/',PRIVATE+'/udev/',1)
                    copy(src,private)
                    if '/rules.d/' in private:
                        p=args.destdir/private.lstrip('/')
                        text=p.read_text()
                        # PROGRAM/RUN relative helpers follow UDEVLIBEXECDIR;
                        # rewrite only the helpers actually built by this profile.
                        for name in ['ata_id','cdrom_id','scsi_id','v4l_id','mtd_probe','fido_id','iocost','dmi_memory_id']:
                            text=text.replace('="'+name+' ', '="'+PRIVATE+'/udev/'+name+' ')
                        p.write_text(text)
        command([args.destdir/'usr/bin/lilly-hwdb','--root='+str(args.destdir),'update'])
        copied.append('usr/lib/lilly/engines/udev/hwdb.bin')
    else:
        copy(directory/'elogind',PRIVATE+'/lilly-logind',True)
        copy(directory/'loginctl','/usr/bin/lilly-loginctl',True)
        copy(directory/'elogind-inhibit','/usr/bin/lilly-inhibit',True)
        copy(directory/'elogind-uaccess-command',PRIVATE+'/elogind-uaccess-command',True)
        copy(directory/'pam_elogind.so','/usr/lib/security/pam_lilly_login.so',True)
        for pattern in ['src/shared/libelogind-shared-*.so','libelogind.so.*']:
            for lib in directory.glob(pattern):
                if lib.is_file():copy(lib,PRIVATE+'/'+lib.name,True)
        for src,dest in installed.items():
            if not isinstance(dest,str) or not Path(src).is_file():continue
            if dest.startswith('/usr/share/polkit-1/actions/') or dest.startswith('/usr/share/dbus-1/system.d/'):
                copy(src,'/usr/share/lilly/login1/'+Path(dest).name)
            elif dest.startswith('/usr/lib/udev/rules.d/'):
                copy(src,PRIVATE+'/udev/rules.d/'+Path(dest).name)
    for relative in copied:
        if '/rules.d/' not in relative:continue
        rule=args.destdir/relative
        text=rule.read_text().replace('/usr/bin/udevadm','/usr/bin/lilly-udevadm').replace('/usr/bin/loginctl','/usr/bin/lilly-loginctl')
        for name in ['ata_id','cdrom_id','scsi_id','v4l_id','mtd_probe','fido_id','iocost','dmi_memory_id']:
            text=text.replace('="'+name+'"','="'+PRIVATE+'/udev/'+name+'"')
        rule.write_text(text)
    license_dir='/usr/share/licenses/lilly-engines/'+args.engine
    for pattern in ('LICENSE*','LICENSES/*'):
        for f in source(args).glob(pattern):
            if f.is_file():copy(f,license_dir+'/'+f.name)
    copy(state,'/usr/share/lilly/engines/'+args.engine+'.json')
    (args.destdir/'usr/share/lilly/engines'/ (args.engine+'-payload.json')).write_text(json.dumps(copied,indent=2)+'\n')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['build','stage'])
    p.add_argument('--engine',choices=SOURCES,required=True)
    p.add_argument('--sources',type=Path,default=ROOT/'vendor')
    p.add_argument('--build',type=Path,default=ROOT/'build/engines')
    p.add_argument('--jobs',type=int,default=int(os.environ.get('LPM_BUILD_JOBS',min(os.cpu_count() or 1,8))))
    p.add_argument('--destdir',type=Path)
    p.add_argument('--stamp',type=Path)
    args=p.parse_args()
    args.sources=args.sources.resolve();args.build=args.build.resolve()
    if args.jobs<1:p.error('--jobs must be positive')
    if args.command=='build':
        build(args)
        if args.stamp:args.stamp.write_text((args.build/args.engine/'lilly-engine.json').read_text())
    else:
        if not args.destdir:p.error('stage requires --destdir')
        args.destdir=args.destdir.resolve();stage(args)
if __name__=='__main__':
    try:main()
    except (OSError,RuntimeError,subprocess.CalledProcessError) as error:
        print('lilly-engines:',error,file=sys.stderr);raise SystemExit(1)
