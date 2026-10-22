/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <pwd.h>
#include <grp.h>
#include <sys/resource.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include "platform.h"
#include "fsutil.h"
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_UNITS 256
#define MAX_ARGS 64
#define MAX_DEPS 64
#define MAX_LINE 4096
struct unit {
    char *name, *exec, *after, *requires;
    char *argv[MAX_ARGS];
    bool oneshot, restart, done, failed, selected;
    int mark;
    int execfd, policy, priority, nice_level;
    unsigned burst;
    long long start_timeout, stop_timeout, restart_delay, interval, window, started, stop_deadline;
    rlim_t rttime;
    char *affinity;
    bool launching, terminating, timeout_failure, nice_set, restart_always;
    char *listen_path, *socket_service, *user, *group, *runtime_dir;
    mode_t runtime_mode;
    bool is_socket, datagram, manual_stop, restart_requested, socket_owned;
    mode_t socket_mode;
    int listenfd, activation;
    dev_t socket_dev; ino_t socket_ino;
    unsigned starts;
    pid_t pid;
    long long retry;
};
static struct unit units[MAX_UNITS];
static int count, order[MAX_UNITS], norder;
static bool stopping;
static bool persistent, optional_load;
static long long deadline;
static int failed;
static bool pid1;
static int shutdown_action;
static int controlfd = -1;
static const char *control_path;
static bool control_owned;
static int clients[16];
static long long client_deadlines[16];
static void die(const char *s) { fprintf(stderr, "lilly: %s\n", s); if (pid1) platform_emergency(); exit(1); }
static void syserr(const char *s) { perror(s); if (pid1) platform_emergency(); exit(1); }
static void *alloc(size_t n) { void *p = calloc(1, n); if (!p) syserr("calloc"); return p; }
static char *dupstr(const char *s) { char *p = strdup(s); if (!p) syserr("strdup"); return p; }
static long long now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) syserr("clock_gettime");
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
static bool valid_name(const char *s) {
    if (!*s || *s=='.' || strlen(s) > 200 || strstr(s, "..")) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.@-", *s)) return false;
    return true;
}
static int find(const char *name) {
    for (int i = 0; i < count; i++) if (!strcmp(name, units[i].name)) return i;
    return -1;
}
static void set(char **p, const char *s) {
    if (*p) die("duplicate directive");
    *p = dupstr(s);
}
/* Small explicit argv grammar: whitespace, single/double quotes, backslash.
 * No expansion, shell interpretation, specifiers, or command prefixes. */
static void command(struct unit *u) {
    char *s = u->exec;
    int argc = 0;
    while (*s) {
        while (isspace((unsigned char)*s)) s++;
        if (!*s) break;
        if (argc == MAX_ARGS - 1) die("too many ExecStart arguments");
        char *a = alloc(strlen(s) + 1), *out = a;
        char quote = 0;
        while (*s && (quote || !isspace((unsigned char)*s))) {
            char c = *s++;
            if (c == '\\' && quote != '\'') { if (!*s) die("trailing backslash"); *out++ = *s++; }
            else if ((c == '\'' || c == '"') && (!quote || quote == c)) quote = quote ? 0 : c;
            else *out++ = c;
        }
        if (quote) die("unclosed ExecStart quote");
        u->argv[argc++] = a;
    }
    if (!argc || u->argv[0][0] != '/') die("ExecStart needs an absolute executable path");
}
static long number(const char *s, long min, long max) {
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno || !*s || *end || n < min || n > max) die("invalid numeric directive");
    return n;
}
static cpu_set_t affinity_mask(const char *s) {
    cpu_set_t mask; CPU_ZERO(&mask);
    char *copy = dupstr(s), *save;
    bool any = false;
    for (char *t = strtok_r(copy, " ,\t", &save); t; t = strtok_r(NULL, " ,\t", &save)) {
        char *dash = strchr(t, '-');
        if (dash) *dash++ = 0;
        long lo = number(t, 0, CPU_SETSIZE-1), hi = dash ? number(dash, lo, CPU_SETSIZE-1) : lo;
        for (long i = lo; i <= hi; i++) CPU_SET(i, &mask);
        any = true;
    }
    free(copy);
    if (!any) die("empty CPUAffinity");
    return mask;
}
static void parse(const char *dir, const char *name) {
    if (count == MAX_UNITS) die("too many units");
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) die("unit path too long");
    FILE *f = fopen(path, "re");
    if (!f) syserr(path);
    struct unit *u = &units[count++];
    u->name = dupstr(name);
    u->execfd = -1; u->listenfd = -1; u->activation = -1; u->socket_mode = 0600;u->runtime_mode=0755;
    u->is_socket = strlen(name)>7 && !strcmp(name+strlen(name)-7,".socket");
    u->burst = 5; u->interval = 10000; u->start_timeout = 30000;
    u->stop_timeout = 3000; u->restart_delay = 250; u->rttime = 200000;
    unsigned seen = 0;
    char line[MAX_LINE], section[32] = "";
    bool type_seen = false, restart_seen = false;
    while (fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n') && !feof(f)) die("unit line too long");
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            if (strcmp(s, "[Unit]") && strcmp(s, "[Service]") && strcmp(s, "[Socket]")) die("unsupported unit section");
            snprintf(section, sizeof section, "%s", s);
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) die("expected key=value");
        *eq++ = 0;
        char *key = trim(s), *value = trim(eq);
        if (!strcmp(section, "[Unit]")) {
            if (!strcmp(key, "Description")) continue;
            if (!strcmp(key, "Requires")) set(&u->requires, value);
            else if (!strcmp(key, "After")) set(&u->after, value);
            else if (!strcmp(key, "StartLimitBurst")) { if (seen & 1) die("duplicate StartLimitBurst");
                seen |= 1; u->burst = (unsigned)number(value, 1, 1000); }
            else if (!strcmp(key, "StartLimitIntervalSec")) { if (seen & 2) die("duplicate StartLimitIntervalSec");
                seen |= 2; u->interval = number(value, 1, 86400) * 1000; }
            else die("unsupported Unit directive");
        } else if (!strcmp(section, "[Socket]")) {
            if(!u->is_socket) die("Socket section in service");
            if(!strcmp(key,"ListenStream")||!strcmp(key,"ListenDatagram")) {
                set(&u->listen_path,value); u->datagram=!strcmp(key,"ListenDatagram");
            } else if(!strcmp(key,"Service")) set(&u->socket_service,value);
            else if(!strcmp(key,"SocketMode")) {
                if(seen&256) die("duplicate SocketMode");
                seen|=256;
                char *end; errno=0; long mode=strtol(value,&end,8);
                if(errno||!*value||*end||mode<0||mode>0777) die("invalid SocketMode");
                u->socket_mode=(mode_t)mode;
            } else if(!strcmp(key,"Accept")&&!strcmp(value,"no")) { /* one service owns listener */ }
            else die("unsupported Socket directive");
        } else if (!strcmp(section, "[Service]")) {
            if(u->is_socket) die("Service section in socket");
            if(!strcmp(key,"User"))set(&u->user,value);
            else if(!strcmp(key,"Group"))set(&u->group,value);
            else if(!strcmp(key,"RuntimeDirectory"))set(&u->runtime_dir,value);
            else if(!strcmp(key,"RuntimeDirectoryMode")) {
                if(seen&512)die("duplicate RuntimeDirectoryMode");
                seen|=512;
                char *end;errno=0;long mode=strtol(value,&end,8);
                if(errno||!*value||*end||mode<0||mode>0777)die("invalid RuntimeDirectoryMode");
                u->runtime_mode=(mode_t)mode;
            }
            else if (!strcmp(key, "ExecStart")) set(&u->exec, value);
            else if (!strcmp(key, "Type")) {
                if (type_seen) die("duplicate Type");
                type_seen = true;
                if (strcmp(value, "simple") && strcmp(value, "oneshot")) die("unsupported service Type");
                u->oneshot = !strcmp(value, "oneshot");
            } else if (!strcmp(key, "Restart")) {
                if (restart_seen) die("duplicate Restart");
                restart_seen = true;
                if (strcmp(value, "no") && strcmp(value, "always") && strcmp(value, "on-failure")) die("unsupported Restart policy");
                u->restart = strcmp(value, "no") != 0;
                u->restart_always = !strcmp(value, "always");
            } else if (!strcmp(key, "CPUSchedulingPolicy")) {
                if (seen & 4) die("duplicate CPUSchedulingPolicy");
                seen |= 4;
                if (!strcmp(value, "other")) u->policy = SCHED_OTHER;
                else if (!strcmp(value, "batch")) u->policy = SCHED_BATCH;
                else if (!strcmp(value, "idle")) u->policy = SCHED_IDLE;
                else if (!strcmp(value, "fifo")) u->policy = SCHED_FIFO;
                else if (!strcmp(value, "rr")) u->policy = SCHED_RR;
                else die("unsupported CPU scheduling policy");
            } else if (!strcmp(key, "CPUSchedulingPriority")) {
                if (seen & 8) die("duplicate priority");
                seen |= 8;
                u->priority = (int)number(value, 0, 99);
            } else if (!strcmp(key, "CPUAffinity")) set(&u->affinity, value);
            else if (!strcmp(key, "Nice")) {
                if (u->nice_set) die("duplicate Nice");
                u->nice_set = true;
                u->nice_level = (int)number(value, -20, 19);
            } else if (!strcmp(key, "LimitRTTIME")) {
                if (seen & 16) die("duplicate LimitRTTIME");
                seen |= 16;
                u->rttime = (rlim_t)number(value, 1000, 1000000);
            } else if (!strcmp(key, "TimeoutStartSec")) {
                if (seen & 32) die("duplicate TimeoutStartSec");
                seen |= 32;
                u->start_timeout = number(value, 1, 3600) * 1000;
            } else if (!strcmp(key, "TimeoutStopSec")) {
                if (seen & 64) die("duplicate TimeoutStopSec");
                seen |= 64;
                u->stop_timeout = number(value, 1, 300) * 1000;
            } else if (!strcmp(key, "RestartSec")) {
                if (seen & 128) die("duplicate RestartSec");
                seen |= 128;
                u->restart_delay = number(value, 0, 3600) * 1000;
            } else die("unsupported Service directive");
        } else die("directive outside section");
    }
    if (ferror(f)) syserr(path);
    fclose(f);
    if(u->is_socket) {
        if(!u->listen_path || u->listen_path[0]!='/' || strlen(u->listen_path)>=sizeof(((struct sockaddr_un *)0)->sun_path)) die("socket requires an absolute Unix path");
        if(!u->socket_service) {
            char service[256]; snprintf(service,sizeof service,"%.*s.service",(int)(strlen(name)-7),name);
            u->socket_service=dupstr(service);
        }
        if(!valid_name(u->socket_service))die("invalid socket service name");
        return;
    }
    if (!u->exec) die("service missing ExecStart");
    if (u->oneshot && u->restart) die("oneshot restart is not supported");
    if ((u->policy == SCHED_FIFO || u->policy == SCHED_RR) ? u->priority < 1 : u->priority != 0)
        die("RT policies require priority 1..99; other policies require priority 0");
    if (u->affinity) (void)affinity_mask(u->affinity);
    if((u->user&&!fs_name(u->user))||(u->group&&!fs_name(u->group))||(u->runtime_dir&&!fs_name(u->runtime_dir)))die("invalid identity or runtime directory");
    if((seen&512)&&!u->runtime_dir)die("RuntimeDirectoryMode requires RuntimeDirectory");
    command(u);
}
static void load(const char *dir) {
    struct dirent **entries;
    int n = scandir(dir, &entries, NULL, alphasort);
    if (n < 0) { if(optional_load && errno==ENOENT)return; syserr(dir); }
    for (int i = 0; i < n; i++) {
        char *name = entries[i]->d_name;
        size_t len = strlen(name);
        if ((len > 8 && !strcmp(name + len - 8, ".service")) || (len > 7 && !strcmp(name + len - 7, ".socket"))) {
            if (!valid_name(name)) die("invalid service filename");
            if(!optional_load || find(name)<0) parse(dir, name);
        }
        free(entries[i]);
    }
    free(entries);
    if (!count && !optional_load) die("no service units");
}
static int deps(const char *list, int out[MAX_DEPS], bool missing_ok) {
    if (!list) return 0;
    char *copy = dupstr(list), *save;
    int n = 0;
    for (char *t = strtok_r(copy, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        if (!valid_name(t)) die("invalid dependency name");
        int i = find(t);
        if (i < 0) { if (missing_ok) continue; fprintf(stderr, "lilly: missing requirement %s\n", t); die("unresolved hard requirement"); }
        if (n == MAX_DEPS) die("too many dependencies");
        out[n++] = i;
    }
    free(copy);
    return n;
}
static void select_unit(int i) {
    if (units[i].selected) return;
    units[i].selected = true;
    int d[MAX_DEPS], n = deps(units[i].requires, d, false);
    for (int j = 0; j < n; j++) select_unit(d[j]);
}
static void visit(int i) {
    struct unit *u = &units[i];
    if (u->mark == 2) return;
    if (u->mark == 1) die("dependency ordering cycle");
    u->mark = 1;
    int d[MAX_DEPS], n = deps(u->after, d, true);
    for (int j = 0; j < n; j++) if (units[d[j]].selected) visit(d[j]);
    u->mark = 2;
    order[norder++] = i;
}
static void rebuild_plan(void) {
    for(int i=0;i<count;i++)if(units[i].is_socket) {
        int target=find(units[i].socket_service);
        if(target<0 || units[target].is_socket)die("missing socket service");
        for(int j=0;j<i;j++)if(units[j].is_socket&&!strcmp(units[i].socket_service,units[j].socket_service))die("one socket unit per service is currently supported");
    }
    norder=0; for(int i=0;i<count;i++)units[i].mark=0;
    for(int i=0;i<count;i++)if(units[i].selected)visit(i);
}
static void load_system(const char *rootpath) {
    char path[PATH_MAX]; optional_load=true;
    if(snprintf(path,sizeof path,"%s/etc/lilly/system",rootpath)>=(int)sizeof path)die("root path too long");
    load(path);
    if(snprintf(path,sizeof path,"%s/usr/lib/lilly/system",rootpath)>=(int)sizeof path)die("root path too long");
    load(path); optional_load=false;
    /* Validate the full catalog before accepting runtime start requests. */
    for(int i=0;i<count;i++)select_unit(i);
    for(int i=0;i<count;i++)if(units[i].is_socket) {
        int target=find(units[i].socket_service);
        if(target<0 || units[target].is_socket)die("missing socket service");
    }
    rebuild_plan();
    for(int i=0;i<count;i++){units[i].selected=false;units[i].mark=0;}norder=0;
    int root=fs_root(rootpath);if(root<0)syserr(rootpath);
    int fd=fs_dir(root,"etc/lilly/enabled",0); close(root);
    if(fd<0){if(errno==ENOENT)return;syserr("enabled directory");}
    DIR *d=fdopendir(fd);if(!d)syserr("enabled directory");
    struct dirent *e;
    while((e=readdir(d))) {
        if(e->d_name[0]=='.')continue;
        struct stat st;
        if(fstatat(dirfd(d),e->d_name,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode))die("enabled marker must be regular");
        int i=find(e->d_name);if(i<0)die("enabled unit not found");select_unit(i);
    }
    closedir(d);rebuild_plan();
}
static void socket_close(struct unit *u) {
    if(u->listenfd>=0)close(u->listenfd);
    u->listenfd=-1;
    struct stat st;
    if(u->socket_owned && !lstat(u->listen_path,&st) && st.st_dev==u->socket_dev && st.st_ino==u->socket_ino)unlink(u->listen_path);
    u->socket_owned=false;
}
static void socket_start(struct unit *u) {
    char path[PATH_MAX];snprintf(path,sizeof path,"%s",u->listen_path);
    char *slash=strrchr(path,'/');*slash=0;
    int root=fs_root("/");if(root<0)syserr("root");
    int dir=fs_dir(root,path,1);close(root);if(dir<0){u->failed=true;failed=1;perror("socket parent");return;}close(dir);
    int fd=socket(AF_UNIX,(u->datagram?SOCK_DGRAM:SOCK_STREAM)|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0){u->failed=true;failed=1;perror("socket");return;}
    if(u->datagram){int one=1;if(setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&one,sizeof one)){close(fd);u->failed=true;failed=1;return;}}
    struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,u->listen_path);
    mode_t old=umask(0777);int result=bind(fd,(struct sockaddr *)&a,sizeof a);umask(old);
    if(result){close(fd);u->failed=true;failed=1;perror("socket bind (existing path preserved)");return;}
    struct stat st;
    if(lstat(u->listen_path,&st)){close(fd);u->failed=true;failed=1;return;}
    u->socket_dev=st.st_dev;u->socket_ino=st.st_ino;u->socket_owned=true;u->listenfd=fd;
    if(chmod(u->listen_path,u->socket_mode)||(!u->datagram&&listen(fd,128))) {socket_close(u);u->failed=true;failed=1;return;}
    fprintf(stderr,"lilly: listening %s %s\n",u->name,u->listen_path);
}
static void request_start(int i,bool seen[MAX_UNITS]) {
    if(seen[i])return;
    seen[i]=true;
    struct unit *u=&units[i];u->selected=true;
    int d[MAX_DEPS],n=deps(u->requires,d,false);for(int j=0;j<n;j++)request_start(d[j],seen);
    u->failed=false;u->manual_stop=false;
    if(!u->pid){u->done=false;u->timeout_failure=false;u->retry=0;u->starts=0;u->window=0;}
}
static void request_stop(int i) {
    units[i].manual_stop=true;
    if(units[i].is_socket) {
        int target=find(units[i].socket_service);
        if(target>=0 && units[target].activation==i)units[target].manual_stop=true;
    }
    bool changed;
    do {
        changed=false;
        for(int j=0;j<count;j++)if(units[j].selected&&!units[j].manual_stop) {
            int d[MAX_DEPS],n=deps(units[j].requires,d,false);
            for(int k=0;k<n;k++)if(units[d[k]].manual_stop){units[j].manual_stop=true;changed=true;break;}
        }
    }while(changed);
}
/* Requires pulls a unit in; After orders it. This preserves the distinction
 * used by systemd rather than implicitly treating Requires as After. */
static bool ready(struct unit *u) {
    int d[MAX_DEPS], n = deps(u->requires, d, false);
    for (int j = 0; j < n; j++) if (units[d[j]].failed) { u->failed = true; failed = 1; return false; }
    n = deps(u->after, d, true);
    for (int j = 0; j < n; j++) if (units[d[j]].selected) {
        struct unit *p = &units[d[j]];
        if (p->failed) continue; /* After alone is ordering, not a hard dependency. */
        if(p->is_socket) {if(p->listenfd<0)return false;continue;}
        if (p->timeout_failure || p->manual_stop) return false;
        if (p->oneshot ? !p->done : (p->launching || (!p->pid && !p->done))) return false;
    }
    return true;
}
static int service_identity(struct unit *u) {
    uid_t uid=geteuid();gid_t gid=getegid();
    if(u->user) {
        errno=0;struct passwd *pw=getpwnam(u->user);if(!pw){if(!errno)errno=ENOENT;return -1;}
        uid=pw->pw_uid;gid=pw->pw_gid;
    }
    if(u->group) {
        errno=0;struct group *gr=getgrnam(u->group);if(!gr){if(!errno)errno=ENOENT;return -1;}gid=gr->gr_gid;
    }
    if(u->runtime_dir) {
        int root=fs_root("/run");if(root<0)return -1;
        int d=fs_dir(root,u->runtime_dir,1);int e=errno;close(root);if(d<0){errno=e;return -1;}
        int r=fchown(d,uid,gid);if(!r)r=fchmod(d,u->runtime_mode);e=errno;close(d);if(r){errno=e;return -1;}
    }
    if(u->user||u->group) {
        if(u->user ? initgroups(u->user,gid) : setgroups(0,NULL))return -1;
        if(setresgid(gid,gid,gid))return -1;
        if(u->user&&setresuid(uid,uid,uid))return -1;
        if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0))return -1;
    }
    return 0;
}
/* Exec is asynchronous: a slow child setup cannot stall PID 1. */
static void start(struct unit *u) {
    if(u->activation<0)for(int i=0;i<count;i++)if(units[i].is_socket && units[i].listenfd>=0 && !strcmp(units[i].socket_service,u->name))u->activation=i;
    if (!u->window || now() - u->window >= u->interval) { u->window = now(); u->starts = 0; }
    if (u->starts >= u->burst) {
        fprintf(stderr, "lilly: start limit hit %s\n", u->name);
        u->failed = true; failed = 1; return;
    }
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK)) syserr("pipe2");
    pid_t pid = fork();
    if (pid < 0) syserr("fork");
    if (!pid) {
        close(pipefd[0]);
        sigset_t empty; sigemptyset(&empty);
        int e = 0;
        for (int sig = 1; sig < NSIG; sig++) if (sig != SIGKILL && sig != SIGSTOP) signal(sig, SIG_DFL);
        if (setpgid(0, 0) || sigprocmask(SIG_SETMASK, &empty, NULL)) e = errno;
        if (!e && pid1 && platform_join(u->name)) e = errno;
        if (!e && u->affinity) { cpu_set_t m = affinity_mask(u->affinity); if (sched_setaffinity(0, sizeof m, &m)) e = errno; }
        if (!e && u->nice_set && setpriority(PRIO_PROCESS, 0, u->nice_level)) e = errno;
        struct sched_param param = { .sched_priority = u->priority };
        if (!e && (u->policy == SCHED_FIFO || u->policy == SCHED_RR)) {
            struct rlimit limit = { .rlim_cur = u->rttime, .rlim_max = u->rttime };
            if (setrlimit(RLIMIT_RTTIME, &limit)) e = errno;
        }
        if (!e && sched_setscheduler(0, u->policy | SCHED_RESET_ON_FORK, &param)) e = errno;
        unsetenv("LISTEN_PID");unsetenv("LISTEN_FDS");unsetenv("LISTEN_FDNAMES");
        if(!e && u->activation>=0) {
            int listener=units[u->activation].listenfd;
            if(listener<0 || dup2(listener,3)<0 || fcntl(3,F_SETFD,0)) e=errno?errno:EBADF;
            char value[32];snprintf(value,sizeof value,"%ld",(long)getpid());
            if(!e && (setenv("LISTEN_PID",value,1)||setenv("LISTEN_FDS","1",1)||setenv("LISTEN_FDNAMES",units[u->activation].name,1))) e=errno;
        }
        if(!e&&service_identity(u))e=errno;
        if (!e) { execv(u->argv[0], u->argv); e = errno; }
        ssize_t written = write(pipefd[1], &e, sizeof e);
        if (written != sizeof e) _exit(126);
        _exit(127);
    }
    close(pipefd[1]);
    u->starts++; u->pid = pid; u->execfd = pipefd[0]; u->launching = true;
    u->started = now(); u->timeout_failure = false; u->terminating = false;
}
static void exec_result(struct unit *u) {
    if (u->execfd < 0) return;
    int e = 0;
    ssize_t r = read(u->execfd, &e, sizeof e);
    if (r < 0 && (errno == EAGAIN || errno == EINTR)) return;
    close(u->execfd); u->execfd = -1; u->launching = false;
    if (r != 0) {
        fprintf(stderr, "lilly: %s exec failed: %s\n", u->name, strerror(e ? e : EIO));
        u->timeout_failure = true;
    } else if (!u->timeout_failure) fprintf(stderr, "lilly: started %s pid=%ld\n", u->name, (long)u->pid);
}
static void terminate_unit(struct unit *u) {
    if (!u->pid || u->terminating) return;
    u->terminating = true; u->stop_deadline = now() + u->stop_timeout;
    kill(-u->pid, SIGTERM); kill(u->pid, SIGTERM);
    fprintf(stderr, "lilly: stopping %s\n", u->name);
}
static void stop_all(void) {
    stopping = true; deadline = now() + 300000;
}
static void stop_ordered(void) {
    for (int j = norder - 1; j >= 0; j--) {
        int i = order[j]; struct unit *u = &units[i]; bool dependent = false;
        if(u->is_socket && (stopping||u->manual_stop)){socket_close(u);u->done=true;continue;}
        if (!u->pid || u->terminating || (!stopping&&!u->manual_stop)) continue;
        for (int k = 0; k < norder; k++) {
            struct unit *v = &units[order[k]];
            if (!v->pid || v == u || (!stopping&&!v->manual_stop)) continue;
            int d[MAX_DEPS], n = deps(v->after, d, true);
            for (int t = 0; t < n; t++) if (d[t] == i) dependent = true;
        }
        if (!dependent) terminate_unit(u);
    }
}
static void propagate_failures(void) {
    bool changed;
    do {
        changed = false;
        for (int j = 0; j < norder; j++) {
            struct unit *u = &units[order[j]];
            if (u->failed) continue;
            int d[MAX_DEPS], n = deps(u->requires, d, false);
            for (int t = 0; t < n; t++) if (units[d[t]].failed) {
                u->failed = true; failed = 1; changed = true;
                terminate_unit(u); break;
            }
        }
    } while (changed);
}
static void control_open(const char *path) {
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) die("control socket path too long");
    controlfd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (controlfd < 0) syserr("control socket");
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    strcpy(a.sun_path, path);
    mode_t old = umask(0077);
    int r = bind(controlfd, (struct sockaddr *)&a, sizeof a);
    umask(old);
    if (r) syserr("bind control socket (will not replace an existing path)");
    control_path = path; control_owned = true;
    if (listen(controlfd, 16)) syserr("listen");
}
static void control_accept(void) {
    int c = accept4(controlfd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (c < 0) return;
    struct ucred cred; socklen_t len = sizeof cred;
    if (getsockopt(c, SOL_SOCKET, SO_PEERCRED, &cred, &len) || cred.uid != (pid1 ? 0 : geteuid())) { close(c); return; }
    for (int i=0; i<16; i++) if (clients[i] < 0) {
        clients[i]=c; client_deadlines[i]=now()+1000; return;
    }
    close(c);
}
static void control_request(int c) {
    char request[256]; ssize_t n = recv(c, request, sizeof request - 1, MSG_DONTWAIT);
    if (n <= 0) return;
    request[n] = 0;
    char response[32768]; size_t used = 0;
    char *argument=strchr(request,' ');if(argument)*argument++=0;
    if(argument && (!valid_name(argument)||find(argument)<0)) {
        ssize_t sent=send(c,"error: unit not found\n",22,MSG_NOSIGNAL);(void)sent;return;
    }
    if (!strcmp(request, "status")) {
        used = (size_t)snprintf(response, sizeof response, "lilly pid=%ld state=%s\n", (long)getpid(), stopping ? "stopping" : "running");
        for (int j = 0; j < count; j++) {
            struct unit *u = &units[j];
            if(argument&&strcmp(argument,u->name))continue;
            const char *state = u->failed ? "failed" : u->listenfd>=0 ? "listening" : u->manual_stop ? "inactive" : !u->selected ? "disabled" : u->terminating ? "stopping" : u->launching ? "starting" : u->pid ? "running" : u->done ? "completed" : "waiting";
            int r = snprintf(response + used, sizeof response - used, "%s %s pid=%ld\n", u->name, state, (long)u->pid);
            if (r < 0 || (size_t)r >= sizeof response - used) break;
            used += (size_t)r;
        }
    } else if(argument && (!strcmp(request,"start")||!strcmp(request,"stop")||!strcmp(request,"restart"))) {
        if(stopping) used=(size_t)snprintf(response,sizeof response,"error: manager shutting down\n");
        else {
            int i=find(argument);struct unit *u=&units[i];
            if(!strcmp(request,"stop"))request_stop(i);
            else {
                bool seen[MAX_UNITS]={false};request_start(i,seen);
                if(!strcmp(request,"restart")) {
                    if(u->is_socket){
                        int target=find(u->socket_service);
                        if(target>=0&&units[target].activation==i)request_stop(target);
                        socket_close(u);u->done=false;
                    }
                    else if(u->pid){u->manual_stop=true;u->restart_requested=true;}
                }
                rebuild_plan();
            }
            used=(size_t)snprintf(response,sizeof response,"accepted\n");
        }
    } else if (!strcmp(request, "poweroff") || !strcmp(request, "reboot") || !strcmp(request, "stop")) {
        if (!strcmp(request, "stop") && pid1) used = (size_t)snprintf(response, sizeof response, "error: PID 1 supports poweroff/reboot, not exit\n");
        else { shutdown_action = !strcmp(request, "reboot"); stop_all(); used = (size_t)snprintf(response, sizeof response, "accepted\n"); }
    } else used = (size_t)snprintf(response, sizeof response, "error: unknown command\n");
    ssize_t sent = send(c, response, used, MSG_NOSIGNAL);
    (void)sent;
}
static int control_client(const char *path, const char *action) {
    if (strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) die("socket path too long");
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) syserr("socket");
    struct timeval timeout = { .tv_sec = 5 };
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout)) syserr("setsockopt");
    struct sockaddr_un a = { .sun_family = AF_UNIX }; strcpy(a.sun_path, path);
    if (connect(fd, (struct sockaddr *)&a, sizeof a)) syserr("connect");
    if (send(fd, action, strlen(action), MSG_NOSIGNAL) != (ssize_t)strlen(action)) syserr("send");
    char response[32768]; ssize_t n = recv(fd, response, sizeof response-1, 0);
    if (n <= 0) syserr("control reply");
    response[n] = 0; fputs(response, stdout); close(fd);
    return !strncmp(response, "error:", 6);
}
static void signal_handler(int sig) { (void)sig; }
static int run(void) {
    sigset_t mask, old; sigemptyset(&mask);
    int signals[] = {SIGCHLD, SIGTERM, SIGINT, SIGUSR1, SIGUSR2, SIGPWR, SIGHUP};
    for (size_t i = 0; i < sizeof signals/sizeof signals[0]; i++) {
        struct sigaction sa = { .sa_handler = signal_handler }; sigemptyset(&sa.sa_mask);
        if (sigaction(signals[i], &sa, NULL)) syserr("sigaction");
        sigaddset(&mask, signals[i]);
    }
    if (sigprocmask(SIG_BLOCK, &mask, &old)) syserr("sigprocmask");
    if (!pid1 && prctl(PR_SET_CHILD_SUBREAPER, 1)) syserr("subreaper");
    int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    int timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd < 0 || timer < 0) syserr("event descriptors");
    struct itimerspec tick = { .it_interval = { .tv_nsec = 20000000 }, .it_value = { .tv_nsec = 20000000 } };
    if (timerfd_settime(timer, 0, &tick, NULL)) syserr("timerfd_settime");
    if (pid1) fprintf(stderr, "lilly: PID 1 operational\n");
    for (;;) {
        propagate_failures();
        bool pending = false, live = false;
        if (!stopping) for (int j = 0; j < norder; j++) {
            struct unit *u = &units[order[j]];
            if(u->manual_stop)continue;
            if(u->is_socket) {
                if(u->listenfd<0&&!u->failed&&!u->done&&ready(u))socket_start(u);
                if(u->listenfd>=0)live=true;
                continue;
            }
            if (!u->pid && !u->done && !u->failed) {
                if (u->retry <= now() && ready(u)) start(u);
                if (!u->failed) pending = true;
            }
        }
        stop_ordered();
        for (int j = 0; j < norder; j++) {
            struct unit *u = &units[order[j]];
            if (!u->pid) continue;
            live = true;
            if (!stopping && !u->terminating && (u->launching || u->oneshot) && now()-u->started >= u->start_timeout) {
                fprintf(stderr, "lilly: start timeout %s\n", u->name); u->timeout_failure = true; terminate_unit(u);
            }
            if (u->terminating && now() >= u->stop_deadline) {
                if (pid1) platform_kill(u->name);
                kill(-u->pid, SIGKILL); kill(u->pid, SIGKILL);
            }
        }
        if (!live && (!pending || stopping) && ((!pid1&&!persistent) || stopping)) break;
        if (stopping && now() >= deadline) { if (pid1) platform_shutdown(shutdown_action); die("shutdown deadline exceeded"); }
        struct pollfd p[MAX_UNITS * 2 + 19]; int n = 19;
        p[0] = (struct pollfd){ .fd=fd, .events=POLLIN };
        p[1] = (struct pollfd){ .fd=timer, .events=POLLIN };
        p[2] = (struct pollfd){ .fd=controlfd, .events=POLLIN };
        for (int i=0; i<16; i++) p[3+i]=(struct pollfd){ .fd=clients[i], .events=POLLIN };
        for (int j=0; j<norder; j++) if (units[order[j]].execfd >= 0)
            p[n++] = (struct pollfd){ .fd=units[order[j]].execfd, .events=POLLIN };
        int socket_index[MAX_UNITS],nsockets=0,begin=n;
        for(int i=0;i<count;i++)if(units[i].listenfd>=0 && !units[i].manual_stop && !stopping) {
            int target=find(units[i].socket_service);
            if(units[target].pid || units[target].failed || (units[target].selected&&!units[target].done&&!units[target].manual_stop))continue;
            socket_index[nsockets++]=i;
            p[n++]=(struct pollfd){.fd=units[i].listenfd,.events=POLLIN};
        }
        if (poll(p, (nfds_t)n, -1) < 0 && errno != EINTR) syserr("poll");
        for(int j=0;j<nsockets;j++)if(p[begin+j].revents&POLLIN) {
            int i=socket_index[j],target=find(units[i].socket_service);struct unit *u=&units[target];
            if(!u->pid&&!u->failed) {
                select_unit(target);u->manual_stop=false;u->done=false;u->activation=i;rebuild_plan();
            }
        }
        if (p[2].revents & POLLIN) control_accept();
        for (int i=0; i<16; i++) if (clients[i]>=0 && (p[3+i].revents || now()>=client_deadlines[i])) {
            if (p[3+i].revents & POLLIN) control_request(clients[i]);
            close(clients[i]); clients[i]=-1;
        }
        uint64_t ticks; ssize_t bytes = read(timer, &ticks, sizeof ticks); (void)bytes;
        struct signalfd_siginfo info;
        while (read(fd, &info, sizeof info) == sizeof info) {
            if (info.ssi_signo == SIGUSR1) fprintf(stderr, "lilly: status pid=%ld selected=%d failed=%d\n", (long)getpid(), norder, failed);
            else if (info.ssi_signo == SIGHUP) fprintf(stderr, "lilly: live reload is not implemented\n");
            else if (info.ssi_signo != SIGCHLD && !stopping) {
                shutdown_action = info.ssi_signo == SIGINT; stop_all();
            }
        }
        for (int j=0; j<norder; j++) exec_result(&units[order[j]]);
        int status; pid_t child;
        while ((child=waitpid(-1, &status, WNOHANG)) > 0) {
            for (int j=0; j<norder; j++) {
                struct unit *u=&units[order[j]]; if (u->pid != child) continue;
                if (u->execfd >= 0) exec_result(u);
                if (pid1) platform_kill(u->name);
                kill(-child, SIGKILL); u->pid=0; u->launching=false;
                bool ok=WIFEXITED(status) && WEXITSTATUS(status)==0 && !u->timeout_failure;
                fprintf(stderr, "lilly: exited %s status=%d\n", u->name, status);
                if(u->restart_requested&&!stopping) {
                    u->restart_requested=false;u->manual_stop=false;u->done=false;u->failed=false;u->timeout_failure=false;u->retry=0;
                } else if(u->manual_stop)u->done=true;
                else if (!stopping && !u->failed && u->restart && (!ok || u->restart_always)) u->retry=now()+u->restart_delay;
                else if (!stopping && (!ok || u->failed)) { u->failed=true; failed=1; }
                else u->done=true;
            }
        }
    }
    close(fd); close(timer);
    if (pid1) platform_shutdown(shutdown_action);
    sigprocmask(SIG_SETMASK, &old, NULL);
    return failed;
}
static void cleanup(void) {
    for (int i=0; i<16; i++) if (clients[i]>=0) close(clients[i]);
    if (controlfd >= 0) close(controlfd);
    if (control_owned) unlink(control_path);
    for (int i = 0; i < count; i++) {
        if(units[i].is_socket)socket_close(&units[i]);
        free(units[i].user);free(units[i].group);free(units[i].runtime_dir);
        free(units[i].listen_path);free(units[i].socket_service);
        free(units[i].affinity);
        free(units[i].name); free(units[i].exec); free(units[i].after); free(units[i].requires);
        for (int j = 0; units[i].argv[j]; j++) free(units[i].argv[j]);
    }
}
int main(int argc, char **argv) {
    for (int i=0; i<16; i++) clients[i]=-1;
    pid1 = getpid() == 1;
    if (pid1) platform_early();
    if (atexit(cleanup)) die("atexit failed");
    if (!pid1 && argc == 2 && !strcmp(argv[1], "--version")) { puts("lilly 0.4.0-dev"); return 0; }
    if (!pid1 && argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: lilly {verify|plan|run} UNIT_DIRECTORY [SERVICE]\nUse lilly init [UNIT_DIRECTORY] as PID 1; lilly ctl SOCKET {status|stop|poweroff|reboot}."); return 0;
    }
    if (!pid1 && argc == 4 && !strcmp(argv[1], "ctl")) return control_client(argv[2], argv[3]);
    if (pid1) {
        if (argc > 3 || (argc >= 2 && strcmp(argv[1], "init"))) die("PID 1 expects init [UNIT_DIRECTORY]");
        const char *dir = argc == 3 ? argv[2] : "/etc/lilly/services";
        control_open("/run/lilly/control");
        if(argc==3 || access("/etc/lilly/services",F_OK)==0) {
            load(dir);for(int i=0;i<count;i++)select_unit(i);rebuild_plan();
        } else load_system("/");
        return run();
    }
    if(argc==3&&!strcmp(argv[1],"verify-system")) {
        load_system(argv[2]);puts("system catalog verified");return 0;
    }
    if(argc==3&&!strcmp(argv[1],"manager")) {
        persistent=true;
        const char *socket_path=getenv("LILLY_CONTROL_SOCKET");
        if(!socket_path)die("foreground manager requires LILLY_CONTROL_SOCKET");
        control_open(socket_path);load_system(argv[2]);return run();
    }
    if (argc >= 2 && !strcmp(argv[1], "init")) die("init requires PID 1");
    const char *socket_path = getenv("LILLY_CONTROL_SOCKET");
    if (socket_path && argc >= 2 && !strcmp(argv[1], "run")) control_open(socket_path);
    if (argc < 3 || argc > 4) die("usage: lilly {verify|plan|run} UNIT_DIRECTORY [SERVICE]");
    bool verify = !strcmp(argv[1], "verify"), plan = !strcmp(argv[1], "plan");
    if (!verify && !plan && strcmp(argv[1], "run")) die("unknown command");
    load(argv[2]);
    if (argc == 4) { int i = find(argv[3]); if (i < 0) die("unknown requested service"); select_unit(i); }
    else for (int i = 0; i < count; i++) select_unit(i);
    rebuild_plan();
    if (verify) { puts("units verified"); return 0; }
    if (plan) { for (int i = 0; i < norder; i++) puts(units[order[i]].name); return 0; }
    return run();
}
