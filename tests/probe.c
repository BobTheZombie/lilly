/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
static int connect_control(void) {
    int fd=socket(AF_UNIX, SOCK_SEQPACKET, 0);
    struct sockaddr_un a={.sun_family=AF_UNIX}; strcpy(a.sun_path,"/run/lilly/control");
    if (fd<0 || connect(fd,(struct sockaddr *)&a,sizeof a)) return -1;
    struct timeval t={.tv_sec=3};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t)) return -1;
    return fd;
}
static int control_test(void) {
    int fd=connect_control(); if(fd<0) return 1;
    struct timespec delay={.tv_nsec=100000000}; nanosleep(&delay,NULL);
    if(send(fd,"status",6,MSG_NOSIGNAL)!=6) return 1;
    char buf[32768]; ssize_t n=recv(fd,buf,sizeof buf-1,0);
    if(n<=0) return 1;
    buf[n]=0; if(!strstr(buf,"lilly pid=1 state=running")) return 1;
    close(fd); puts("LILLY_CONTROL_DELAY_OK"); fflush(stdout);
    /* Temporarily loosen filesystem access to exercise SO_PEERCRED itself. */
    if(chmod("/run/lilly",0755) || chmod("/run/lilly/control",0666)) return 1;
    pid_t child=fork(); if(child<0) return 1;
    if(!child) {
        if(setgid(65534) || setuid(65534)) _exit(1);
        int c=connect_control(); if(c<0) _exit(1);
        ssize_t sent=send(c,"poweroff",8,MSG_NOSIGNAL); (void)sent;
        n=recv(c,buf,sizeof buf,0); close(c);
        _exit(n>0 || (n<0 && errno!=ECONNRESET));
    }
    int status; if(waitpid(child,&status,0)<0) return 1;
    if(chmod("/run/lilly",0700) || chmod("/run/lilly/control",0600)) return 1;
    if(!WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    puts("LILLY_CONTROL_AUTH_OK"); return 0;
}
int main(int argc, char **argv) {
    if (argc==2 && !strcmp(argv[1],"control-test")) return control_test();
    if (argc==2 && !strcmp(argv[1],"spin")) {
        volatile unsigned long n=0; for(;;) n++;
    }
    struct sched_param p; struct rlimit lim;
    if(sched_getparam(0,&p) || getrlimit(RLIMIT_RTTIME,&lim)) return 1;
    printf("LILLY_PROBE pid=%ld policy=%d priority=%d cpu=%d rttime=%lu\n",
           (long)getpid(),sched_getscheduler(0)&~SCHED_RESET_ON_FORK,p.sched_priority,sched_getcpu(),(unsigned long)lim.rlim_cur);
    fflush(stdout);
    pid_t child=fork(); if(child<0) return 1;
    if(!child) {
        int policy=sched_getscheduler(0)&~SCHED_RESET_ON_FORK;
        printf("LILLY_PROBE fork_policy=%d\n",policy); fflush(stdout); _exit(policy!=SCHED_OTHER);
    }
    int status; if(waitpid(child,&status,0)<0) return 1;
    return !WIFEXITED(status) || WEXITSTATUS(status);
}
