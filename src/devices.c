/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef LILLY_UDEVD
#define LILLY_UDEVD "/usr/bin/udevd"
#endif
#ifndef LILLY_UDEVADM
#define LILLY_UDEVADM "/usr/bin/udevadm"
#endif
/* The real udev provider owns netlink, rules, workers, /run/udev and libudev.
 * This tool supplies bounded coldplug/readiness and an explicit provider ABI. */
static volatile sig_atomic_t interrupted;
static void stop(int sig) { interrupted=sig; }
static double monotime(void) {
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)) {perror("clock_gettime");exit(1);}
    return ts.tv_sec+ts.tv_nsec/1e9;
}
static int run(char *const argv[], double deadline, int quiet) {
    if(interrupted || monotime()>=deadline)return 124;
    pid_t pid=fork();
    if(pid<0){perror("fork");return 1;}
    if(!pid) {
        if(setpgid(0,0))_exit(126);
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
        if(quiet && !freopen("/dev/null","w",stderr))_exit(126);
        execv(argv[0],argv);_exit(errno==ENOENT?127:126);
    }
    /* Both sides set the group; EACCES means the child has already exec'd. */
    if(setpgid(pid,pid) && errno!=EACCES && errno!=ESRCH) {
        kill(pid,SIGKILL);waitpid(pid,NULL,0);return 1;
    }
    int status;
    for(;;) {
        pid_t r=waitpid(pid,&status,WNOHANG);
        if(r==pid)return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
        if(r<0 && errno!=EINTR)return 1;
        if(interrupted || monotime()>=deadline) {
            kill(-pid,SIGKILL);kill(pid,SIGKILL);
            while(waitpid(pid,&status,0)<0 && errno==EINTR) {}
            return interrupted?128+interrupted:124;
        }
        struct timespec delay={0,20000000};nanosleep(&delay,NULL);
    }
}
static int ready(double deadline) {
    /* Acknowledged reload works with both eudev and systemd-udevd. Merely
     * finding /run/udev/control does not prove a live daemon is responding. */
    char *args[]={LILLY_UDEVADM,"control","--timeout=1","--reload",NULL};
    int r;
    do {
        r=run(args,deadline,1);
        if(!r)return 0;
        if(r==127 || r==126 || interrupted)return r;
        struct timespec delay={0,100000000};nanosleep(&delay,NULL);
    } while(monotime()<deadline);
    fprintf(stderr,"lilly-devices: udev readiness deadline expired\n");return 124;
}
int main(int argc,char **argv) {
    int seconds=30,opt;
    static const struct option opts[]={{"timeout",required_argument,NULL,'t'}, {"help",no_argument,NULL,'h'}, {NULL,0,NULL,0}};
    while((opt=getopt_long(argc,argv,"+",opts,NULL))!=-1) {
        if(opt=='t') {
            char *end;errno=0;long n=strtol(optarg,&end,10);
            if(errno||!*optarg||*end||n<1||n>600)return 2;
            seconds=(int)n;
        } else {
            puts("Usage: lilly-devices [--timeout 1..600] {daemon|ready|coldplug|settle|reload|trigger|info|monitor|test|test-builtin} [udev arguments]");
            printf("Provider: %s; administration: %s\n",LILLY_UDEVD,LILLY_UDEVADM);
            return opt=='h'?0:2;
        }
    }
    if(optind>=argc)return 2;
    const char *cmd=argv[optind++];
    if(!strcmp(cmd,"daemon")) {
        if(optind!=argc || geteuid()!=0)return 2;
        execl(LILLY_UDEVD,LILLY_UDEVD,"--resolve-names=early",(char *)NULL);
        perror(LILLY_UDEVD);return 127;
    }
    if(!strcmp(cmd,"info")||!strcmp(cmd,"monitor")||!strcmp(cmd,"test")||!strcmp(cmd,"test-builtin")||!strcmp(cmd,"trigger")) {
        /* No shell, PATH lookup, inherited executable override or fake protocol. */
        char **args=calloc((size_t)(argc-optind)+3,sizeof *args);if(!args)return 1;
        args[0]=LILLY_UDEVADM;args[1]=(char *)cmd;
        for(int i=optind;i<argc;i++)args[i-optind+2]=argv[i];
        execv(args[0],args);perror(args[0]);free(args);return 127;
    }
    if(optind!=argc)return 2;
    if(strcmp(cmd,"ready")&&strcmp(cmd,"coldplug")&&strcmp(cmd,"settle")&&strcmp(cmd,"reload"))return 2;
    struct sigaction sa={.sa_handler=stop};sigemptyset(&sa.sa_mask);
    if(sigaction(SIGTERM,&sa,NULL)||sigaction(SIGINT,&sa,NULL))return 1;
    double deadline=monotime()+seconds;
    if(!strcmp(cmd,"ready") || !strcmp(cmd,"reload"))return ready(deadline);
    if(!strcmp(cmd,"coldplug")) {
        int r=ready(deadline);if(r)return r;
        char *sub[]={LILLY_UDEVADM,"trigger","--type=subsystems","--action=add",NULL};
        char *dev[]={LILLY_UDEVADM,"trigger","--type=devices","--action=add",NULL};
        r=run(sub,deadline,0);if(r)return r;
        r=run(dev,deadline,0);if(r)return r;
    }
    char timeout[32];snprintf(timeout,sizeof timeout,"--timeout=%d",seconds);
    char *settle[]={LILLY_UDEVADM,"settle",timeout,NULL};
    return run(settle,deadline,0);
}
