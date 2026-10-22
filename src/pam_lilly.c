/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "fsutil.h"
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <security/pam_modules.h>
#include <security/pam_ext.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#ifndef LILLY_RUN_DIR
#define LILLY_RUN_DIR "/run"
#endif
#define DATA_KEY "lilly.session.v1"
struct session { int records; int owned; char id[40]; };
static int root_dir(int fd) {
    struct stat st;
    return !fstat(fd,&st) && S_ISDIR(st.st_mode) && st.st_uid==0 && !(st.st_mode&0022);
}
static void cleanup(pam_handle_t *pamh,void *data,int status) {
    (void)pamh;(void)status;
    struct session *s=data;
    if(s->records>=0) {if(s->owned)unlinkat(s->records,s->id,0);close(s->records);}
    free(s);
}
static void encode(const char *src,char *dst,size_t cap) {
    static const char hex[]="0123456789abcdef";
    size_t used=0;
    if(!src)src="";
    while(*src && used+4<cap) {
        unsigned char c=(unsigned char)*src++;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("/@_.:-",c))dst[used++]=(char)c;
        else {dst[used++]='%';dst[used++]=hex[c>>4];dst[used++]=hex[c&15];}
    }
    dst[used]=0;
}
static int runtime(int run,uid_t uid,gid_t gid) {
    int parent=fs_dir(run,"user",1);
    if(parent<0)return -1;
    if(!root_dir(parent)){close(parent);errno=EPERM;return -1;}
    char name[32];snprintf(name,sizeof name,"%lu",(unsigned long)uid);
    int created=!mkdirat(parent,name,0700);
    if(!created&&errno!=EEXIST){close(parent);return -1;}
    int fd=openat(parent,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    close(parent);if(fd<0)return -1;
    int result=0;
    if(created && (fchown(fd,uid,gid)||fchmod(fd,0700)))result=-1;
    struct stat st;
    if(fstat(fd,&st)||st.st_uid!=uid||(st.st_mode&0777)!=0700)result=-1;
    close(fd);return result;
}
PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh,int flags,int argc,const char **argv) {
    (void)flags;(void)argv;
    if(argc || geteuid()!=0)return PAM_SESSION_ERR;
    const void *existing=NULL;
    if(pam_get_data(pamh,DATA_KEY,&existing)==PAM_SUCCESS && existing)return PAM_SUCCESS;
    const char *user=NULL;
    if(pam_get_user(pamh,&user,NULL)!=PAM_SUCCESS || !user || !*user)return PAM_USER_UNKNOWN;
    struct passwd *pw=getpwnam(user);if(!pw)return PAM_USER_UNKNOWN;
    uid_t uid=pw->pw_uid;gid_t gid=pw->pw_gid;
    int run=fs_root(LILLY_RUN_DIR);
    if(run<0)return PAM_SESSION_ERR;
    if(!root_dir(run) || runtime(run,uid,gid)) {close(run);return PAM_SESSION_ERR;}
    int lilly=fs_dir(run,"lilly",1);close(run);
    if(lilly<0)return PAM_SESSION_ERR;
    if(!root_dir(lilly)){close(lilly);return PAM_SESSION_ERR;}
    int records=fs_dir(lilly,"sessions",1);close(lilly);
    if(records<0)return PAM_SESSION_ERR;
    if(!root_dir(records)||fchmod(records,0700)){close(records);return PAM_SESSION_ERR;}
    struct session *s=calloc(1,sizeof *s);
    if(!s){close(records);return PAM_BUF_ERR;}s->records=records;
    uint64_t nonce[2];ssize_t n;
    do{n=getrandom(nonce,sizeof nonce,0);}while(n<0&&errno==EINTR);
    if(n!=(ssize_t)sizeof nonce){cleanup(pamh,s,0);return PAM_SESSION_ERR;}
    snprintf(s->id,sizeof s->id,"%016llx%016llx",(unsigned long long)nonce[0],(unsigned long long)nonce[1]);
    const void *service=NULL,*tty=NULL,*rhost=NULL;
    pam_get_item(pamh,PAM_SERVICE,&service);pam_get_item(pamh,PAM_TTY,&tty);pam_get_item(pamh,PAM_RHOST,&rhost);
    char u[512],v[512],t[512],h[512],record[2400];
    encode(user,u,sizeof u);encode(service,v,sizeof v);encode(tty,t,sizeof t);encode(rhost,h,sizeof h);
    int len=snprintf(record,sizeof record,"ID=%s\nUID=%lu\nUSER=%s\nSERVICE=%s\nTTY=%s\nREMOTE_HOST=%s\nLEADER=%ld\n",s->id,(unsigned long)uid,u,v,t,h,(long)getpid());
    if(len<0 || (size_t)len>=sizeof record || fs_write(records,s->id,record,(size_t)len,0600,0)) {
        cleanup(pamh,s,0);return PAM_SESSION_ERR;
    }
    s->owned=1;
    if(pam_set_data(pamh,DATA_KEY,s,cleanup)!=PAM_SUCCESS) {cleanup(pamh,s,0);return PAM_SYSTEM_ERR;}
    char env[512];snprintf(env,sizeof env,"XDG_RUNTIME_DIR=" LILLY_RUN_DIR "/user/%lu",(unsigned long)uid);
    int r=pam_putenv(pamh,env);
    if(r==PAM_SUCCESS){snprintf(env,sizeof env,"XDG_SESSION_ID=%s",s->id);r=pam_putenv(pamh,env);}
    if(r!=PAM_SUCCESS){pam_putenv(pamh,"XDG_RUNTIME_DIR");pam_putenv(pamh,"XDG_SESSION_ID");pam_set_data(pamh,DATA_KEY,NULL,NULL);return PAM_SESSION_ERR;}
    pam_syslog(pamh,LOG_INFO,"Lilly session %s opened for uid %lu",s->id,(unsigned long)uid);
    return PAM_SUCCESS;
}
PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh,int flags,int argc,const char **argv) {
    (void)flags;(void)argv;
    if(argc || geteuid()!=0)return PAM_SESSION_ERR;
    const void *data=NULL;
    if(pam_get_data(pamh,DATA_KEY,&data)!=PAM_SUCCESS || !data)return PAM_SUCCESS;
    struct session *s=(struct session *)data;
    /* Never remove a user's shared runtime directory while another session or
     * user service may still hold it. Reboot clears the /run tmpfs. */
    if(s->records>=0) {
        if(unlinkat(s->records,s->id,0) && errno!=ENOENT)return PAM_SESSION_ERR;
        close(s->records);s->records=-1;
    }
    pam_syslog(pamh,LOG_INFO,"Lilly session %s closed",s->id);
    char expected[80];snprintf(expected,sizeof expected,"%s",s->id);
    const char *current=pam_getenv(pamh,"XDG_SESSION_ID");
    if(current&&!strcmp(current,expected))pam_putenv(pamh,"XDG_SESSION_ID");
    return pam_set_data(pamh,DATA_KEY,NULL,NULL)==PAM_SUCCESS?PAM_SUCCESS:PAM_SESSION_ERR;
}
