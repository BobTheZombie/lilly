/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "fsutil.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static void fail(const char *s){perror(s);exit(1);}
static void unit_name(const char *s,char out[201]) {
    if(*s=='.'||!fs_name(s)) {fprintf(stderr,"invalid unit name\n");exit(1);}
    size_t n=strlen(s);
    bool suffix=(n>8&&!strcmp(s+n-8,".service"))||(n>7&&!strcmp(s+n-7,".socket"));
    if(snprintf(out,201,"%s%s",s,suffix?"":".service")>=201)fail("unit name too long");
}
static int request(const char *path,const char *command,const char *unit) {
    struct sockaddr_un a={.sun_family=AF_UNIX};
    if(strlen(path)>=sizeof a.sun_path) {errno=ENAMETOOLONG;fail("socket");}
    strcpy(a.sun_path,path);
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0);if(fd<0)fail("socket");
    struct timeval t={.tv_sec=5};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t)||connect(fd,(struct sockaddr *)&a,sizeof a))fail("connect Lilly");
    char msg[256];snprintf(msg,sizeof msg,"%s%s%s",command,unit?" ":"",unit?unit:"");
    if(send(fd,msg,strlen(msg),MSG_NOSIGNAL)!=(ssize_t)strlen(msg))fail("send");
    char reply[32768];ssize_t n=recv(fd,reply,sizeof reply-1,0);if(n<=0)fail("reply");
    reply[n]=0;fputs(reply,stdout);close(fd);return !strncmp(reply,"error:",6);
}
static int unit_exists(int root,const char *name) {
    char path[512];
    const char *dirs[]={"etc/lilly/system","usr/lib/lilly/system"};
    for(size_t i=0;i<2;i++) {snprintf(path,sizeof path,"%s/%s",dirs[i],name);int fd=fs_open(root,path,O_RDONLY);if(fd>=0){close(fd);return 1;}if(errno!=ENOENT)fail(path);}
    return 0;
}
static int is_enabled(int root,const char *name) {
    char path[512];snprintf(path,sizeof path,"etc/lilly/enabled/%s",name);
    int fd=fs_open(root,path,O_RDONLY);if(fd>=0){close(fd);return 1;}
    if(errno!=ENOENT)fail(path);
    return 0;
}
static int compare(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
static void list(int root) {
    char *names[512];size_t count=0;
    const char *dirs[]={"etc/lilly/system","usr/lib/lilly/system"};
    for(size_t i=0;i<2;i++) {
        int fd=fs_dir(root,dirs[i],0);if(fd<0){if(errno==ENOENT)continue;fail(dirs[i]);}
        DIR *d=fdopendir(fd);if(!d)fail("fdopendir");
        struct dirent *e;
        while((e=readdir(d))) {
            size_t n=strlen(e->d_name);
            if(!((n>8&&!strcmp(e->d_name+n-8,".service"))||(n>7&&!strcmp(e->d_name+n-7,".socket"))))continue;
            if(!fs_name(e->d_name))continue;
            bool seen=false;for(size_t j=0;j<count;j++)if(!strcmp(names[j],e->d_name))seen=true;
            if(seen)continue;
            if(count==512){fprintf(stderr,"too many units\n");exit(1);}
            names[count]=strdup(e->d_name);if(!names[count++])fail("strdup");
        }
        closedir(d);
    }
    qsort(names,count,sizeof *names,compare);
    for(size_t i=0;i<count;i++){printf("%s %s\n",names[i],is_enabled(root,names[i])?"enabled":"disabled");free(names[i]);}
}
int main(int argc,char **argv) {
    const char *rootpath="/",*socketpath="/run/lilly/control";bool now=false,explicit_socket=false;
    static const struct option opts[]={{"root",1,NULL,'r'},{"socket",1,NULL,'s'},{"now",0,NULL,'n'},{"help",0,NULL,'h'},{0,0,0,0}};
    int opt;while((opt=getopt_long(argc,argv,"",opts,NULL))!=-1) {
        if(opt=='r')rootpath=optarg;
        else if(opt=='s'){socketpath=optarg;explicit_socket=true;}
        else if(opt=='n')now=true;
        else {puts("Usage: servicectl [--root ROOT] [--socket PATH] [--now] {enable|disable|is-enabled|list-unit-files|status|start|stop|restart|poweroff|reboot} [UNIT]");return opt=='h'?0:1;}
    }
    if(optind>=argc)return 1;
    const char *cmd=argv[optind++];char name[201];const char *unit=NULL;
    if(optind<argc){unit_name(argv[optind++],name);unit=name;}
    if(optind!=argc)return 1;
    bool persistent=!strcmp(cmd,"enable")||!strcmp(cmd,"disable")||!strcmp(cmd,"is-enabled")||!strcmp(cmd,"list-unit-files");
    if((!persistent||now)&&strcmp(rootpath,"/")&&!explicit_socket){fprintf(stderr,"offline --root requires an explicit --socket for runtime actions\n");return 1;}
    if(persistent) {
        int root=fs_root(rootpath);if(root<0)fail(rootpath);
        if(!strcmp(cmd,"list-unit-files")){if(unit||now)return 1;list(root);close(root);return 0;}
        if(!unit)return 1;
        if(!strcmp(cmd,"is-enabled")){int on=is_enabled(root,unit);puts(on?"enabled":"disabled");close(root);return !on;}
        if(!unit_exists(root,unit)){fprintf(stderr,"unit not found: %s\n",unit);return 1;}
        char path[512];snprintf(path,sizeof path,"etc/lilly/enabled/%s",unit);
        if(!strcmp(cmd,"enable")) {
            if(!is_enabled(root,unit)&&fs_write(root,path,"",0,0644,0))fail("enable unit");
        } else if(fs_remove(root,path)&&errno!=ENOENT)fail("disable unit");
        close(root);
        if(now)return request(socketpath,!strcmp(cmd,"enable")?"start":"stop",unit);
        return 0;
    }
    if(now)return 1;
    if(strcmp(cmd,"status")&&strcmp(cmd,"start")&&strcmp(cmd,"stop")&&strcmp(cmd,"restart")&&strcmp(cmd,"poweroff")&&strcmp(cmd,"reboot")){fprintf(stderr,"unsupported command: %s\n",cmd);return 1;}
    if((!strcmp(cmd,"start")||!strcmp(cmd,"stop")||!strcmp(cmd,"restart"))&&!unit){fprintf(stderr,"unit required\n");return 1;}
    if((!strcmp(cmd,"poweroff")||!strcmp(cmd,"reboot"))&&unit)return 1;
    return request(socketpath,cmd,unit);
}
