/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t running=1,reopen;
static void signal_event(int sig){if(sig==SIGHUP)reopen=1;else running=0;}
int main(int argc,char **argv) {
    const char *output=argc==2?argv[1]:"/var/log/lilly.log";
    if(argc>2)return 1;
    const char *pid=getenv("LISTEN_PID"),*fds=getenv("LISTEN_FDS");
    char expected[32];snprintf(expected,sizeof expected,"%ld",(long)getpid());
    int type=0;socklen_t len=sizeof type;
    if(!pid||strcmp(pid,expected)||!fds||strcmp(fds,"1")||getsockopt(3,SOL_SOCKET,SO_TYPE,&type,&len)||type!=SOCK_DGRAM) {
        fprintf(stderr,"lilly-logd requires one socket-activated datagram fd\n");return 1;
    }
    unsetenv("LISTEN_PID");unsetenv("LISTEN_FDS");unsetenv("LISTEN_FDNAMES");
    if(fcntl(3,F_SETFD,FD_CLOEXEC))return 1;
    struct sigaction sa={.sa_handler=signal_event};sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM,&sa,NULL);sigaction(SIGINT,&sa,NULL);sigaction(SIGHUP,&sa,NULL);
    FILE *log=NULL;
    while(running) {
        if(!log||reopen) {
            int fd=open(output,O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0640);
            if(fd<0){perror(output);return 1;}
            struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);fprintf(stderr,"log output must be a regular file\n");return 1;}
            FILE *next=fdopen(fd,"a");if(!next){close(fd);return 1;}
            if(log)fclose(log);
            log=next;reopen=0;
        }
        struct pollfd p={.fd=3,.events=POLLIN};
        int ready=poll(&p,1,1000);if(ready<0&&errno==EINTR)continue;
        if(ready<0)return 1;
        if(!ready)continue;
        char message[8193],control[CMSG_SPACE(sizeof(struct ucred))];
        struct iovec iov={.iov_base=message,.iov_len=sizeof message-1};
        struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof control};
        ssize_t n=recvmsg(3,&msg,MSG_DONTWAIT);
        struct ucred *cred=NULL;
        for(struct cmsghdr *c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c))
            if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len>=CMSG_LEN(sizeof(struct ucred)))cred=(struct ucred *)CMSG_DATA(c);
        if(n<0){if(errno==EAGAIN||errno==EINTR)continue;return 1;}
        if(!cred){fprintf(stderr,"datagram missing sender credentials\n");continue;}
        for(ssize_t i=0;i<n;i++)if((unsigned char)message[i]<32||message[i]==127)message[i]=' ';
        message[n]=0;
        if(fprintf(log,"%lld uid=%lu pid=%ld %s\n",(long long)time(NULL),(unsigned long)cred->uid,(long)cred->pid,message)<0||fflush(log)){perror("syslog write");return 1;}
    }
    if(log)fclose(log);
    return 0;
}
