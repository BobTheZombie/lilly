/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "platform.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <sched.h>
#include <sys/resource.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <linux/magic.h>

static void on_signal(int sig) { (void)sig; }
static void fail(const char *s) { perror(s); platform_emergency(); }
static void directory(const char *path, mode_t mode) {
    if (mkdir(path, mode) && errno != EEXIST) fail(path);
    struct stat st;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode)) fail(path);
}
static void unescape(char *s);
static bool mounted_at(const char *type, const char *path, long magic) {
    struct statfs fs;
    if (statfs(path, &fs) || fs.f_type != magic) return false;
    FILE *f=fopen("/proc/mounts", "re");
    if (!f) return false;
    char *line=NULL; size_t cap=0; bool found=false;
    while (getline(&line,&cap,f)>0) {
        char source[PATH_MAX], target[PATH_MAX], kind[64];
        if (sscanf(line,"%4095s %4095s %63s",source,target,kind)!=3) continue;
        unescape(target);
        if (!strcmp(target,path)) found=!strcmp(kind,type);
    }
    free(line); fclose(f); return found;
}
static void mount_fs(const char *type, const char *path, unsigned long flags, const char *data, long magic) {
    directory(path, 0755);
    if (mounted_at(type,path,magic)) return;
    if (mount(type, path, type, flags, data)) fail(path);
}
void platform_early(void) {
    if (getpid() != 1) { errno=EPERM; fail("early mounts require PID 1"); }
    umask(0022);
    struct sched_param normal={.sched_priority=0};
    if (sched_setscheduler(0,SCHED_OTHER,&normal) || setpriority(PRIO_PROCESS,0,0)) fail("PID 1 normal scheduling");
    if (reboot(RB_DISABLE_CAD)) fail("disable immediate Ctrl-Alt-Delete reboot");
    if (chdir("/")) fail("chdir");
    /* Prevent subsequent mounts from propagating back to an ancestor namespace. */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) fail("private mounts");
    mount_fs("proc", "/proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL, PROC_SUPER_MAGIC);
    mount_fs("sysfs", "/sys", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL, SYSFS_MAGIC);
    mount_fs("devtmpfs", "/dev", MS_NOSUID, "mode=0755", TMPFS_MAGIC);
    int console=open("/dev/console", O_RDWR|O_NOCTTY);
    if (console >= 0) {
        for (int i=0;i<3;i++) if (dup2(console, i) < 0) fail("console dup2");
        if (console > 2) close(console);
    } else fail("open console");
    setvbuf(stderr, NULL, _IONBF, 0);
    mount_fs("tmpfs", "/run", MS_NOSUID|MS_NODEV, "mode=0755", TMPFS_MAGIC);
    directory("/run/lilly", 0700);
    mount_fs("devpts", "/dev/pts", MS_NOSUID|MS_NOEXEC, "mode=0620,ptmxmode=0666", DEVPTS_SUPER_MAGIC);
    mount_fs("cgroup2", "/sys/fs/cgroup", MS_NOSUID|MS_NOEXEC|MS_NODEV, NULL, CGROUP2_SUPER_MAGIC);
    directory("/sys/fs/cgroup/lilly", 0755);
    if (symlink("/proc/self/fd", "/dev/fd") && errno != EEXIST) fail("/dev/fd");
    if (symlink("/dev/pts/ptmx", "/dev/ptmx") && errno != EEXIST) fail("/dev/ptmx");
    if (setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1)) fail("PATH");
    if (setenv("HOME", "/root", 1)) fail("HOME");
    fprintf(stderr, "lilly: early mounts and cgroup v2 ready\n");
}
int platform_join(const char *name) {
    char path[PATH_MAX];
    int n=snprintf(path,sizeof path,"/sys/fs/cgroup/lilly/%s",name);
    if (n < 0 || (size_t)n >= sizeof path-16) { errno=ENAMETOOLONG; return -1; }
    if (mkdir(path,0755) && errno != EEXIST) return -1;
    strcat(path,"/cgroup.procs");
    int fd=open(path,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
    if (fd<0) return -1;
    ssize_t written=write(fd,"0\n",2); int saved=errno; close(fd); errno=saved;
    return written==2 ? 0 : -1;
}
void platform_kill(const char *name) {
    char path[PATH_MAX];
    int n=snprintf(path,sizeof path,"/sys/fs/cgroup/lilly/%s/cgroup.kill",name);
    if (n < 0 || (size_t)n >= sizeof path) return;
    int fd=open(path,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);
    if (fd>=0) {
        ssize_t written=write(fd,"1\n",2);
        if (written!=2) perror("cgroup.kill");
        close(fd);
    } else if (errno!=ENOENT) perror("cgroup.kill");
}
/* PID 1 must never call exit after a configuration or resource failure.
 * No unauthenticated emergency root shell: an operator can poweroff/reboot
 * using the serial console's SysRq or a signal from a rescue environment. */
_Noreturn void platform_emergency(void) {
    fprintf(stderr,"lilly: EMERGENCY: PID 1 retained; send SIGINT to reboot or SIGTERM to poweroff\n");
    struct sigaction sa={.sa_handler=on_signal}; sigemptyset(&sa.sa_mask);
    int sigs[]={SIGCHLD,SIGINT,SIGTERM,SIGUSR2,SIGPWR};
    sigset_t mask; sigemptyset(&mask);
    for(size_t i=0;i<sizeof sigs/sizeof sigs[0];i++) { sigaction(sigs[i],&sa,NULL); sigaddset(&mask,sigs[i]); }
    sigprocmask(SIG_BLOCK,&mask,NULL);
    for(;;) {
        while(waitpid(-1,NULL,WNOHANG)>0) {}
        int sig=sigwaitinfo(&mask,NULL);
        if(sig==SIGINT || sig==SIGTERM || sig==SIGUSR2 || sig==SIGPWR) platform_shutdown(sig==SIGINT);
    }
}
static void unescape(char *s) {
    char *out=s;
    while(*s) {
        if(s[0]=='\\' && s[1]>='0'&&s[1]<='7' && s[2]>='0'&&s[2]<='7' && s[3]>='0'&&s[3]<='7') {
            *out++=(char)((s[1]-'0')*64+(s[2]-'0')*8+s[3]-'0'); s+=4;
        } else *out++=*s++;
    }
    *out=0;
}
static void unmount_all(void) {
    /* /proc/mounts order is not guaranteed to be dependency order. Retry
     * deepest paths first, with repeated passes for stacked mounts. */
    char paths[256][PATH_MAX]; int count=0;
    FILE *f=fopen("/proc/mounts","re");
    if(f) {
        char *line=NULL; size_t cap=0;
        while(count<256 && getline(&line,&cap,f)>0) {
            char source[PATH_MAX], path[PATH_MAX];
            if(sscanf(line,"%4095s %4095s",source,path)!=2) continue;
            unescape(path);
            if(!strcmp(path,"/") || !strcmp(path,"/proc") || !strcmp(path,"/dev") || !strcmp(path,"/run")) continue;
            strcpy(paths[count++],path);
        }
        free(line); fclose(f);
    }
    for(int i=0;i<count;i++) for(int j=i+1;j<count;j++) if(strlen(paths[j])>strlen(paths[i])) {
        char tmp[PATH_MAX]; memmove(tmp,paths[i],sizeof tmp); memmove(paths[i],paths[j],sizeof tmp); memmove(paths[j],tmp,sizeof tmp);
    }
    sync();
    for(int pass=0;pass<3;pass++) for(int i=0;i<count;i++) {
        if(umount2(paths[i],0) && errno!=EINVAL && errno!=ENOENT && pass==2) {
            if(mount(NULL,paths[i],NULL,MS_REMOUNT|MS_RDONLY,NULL)) perror(paths[i]);
        }
    }
    sync();
    if(mount(NULL,"/",NULL,MS_REMOUNT|MS_RDONLY,NULL)) perror("root remount read-only");
}
_Noreturn void platform_shutdown(int restart) {
    if(getpid()!=1) { errno=EPERM; perror("shutdown requires PID 1"); _exit(1); }
    fprintf(stderr,"lilly: shutting down (%s)\n",restart?"reboot":"poweroff");
    kill(-1,SIGTERM);
    struct timespec pause_time={.tv_nsec=100000000};
    for(int i=0;i<20;i++) { while(waitpid(-1,NULL,WNOHANG)>0) {} nanosleep(&pause_time,NULL); }
    kill(-1,SIGKILL);
    for(int i=0;i<20;i++) {
        pid_t p; while((p=waitpid(-1,NULL,WNOHANG))>0) {}
        if(p<0 && errno==ECHILD) break;
        nanosleep(&pause_time,NULL);
    }
    unmount_all();
    fprintf(stderr,"lilly: shutdown complete\n");
    reboot(restart?RB_AUTOBOOT:RB_POWER_OFF);
    perror("reboot syscall");
    for(;;) pause();
}
