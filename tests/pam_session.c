/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int conversation(int n,const struct pam_message **m,struct pam_response **r,void *p) {
    (void)n;(void)m;(void)r;(void)p;return PAM_CONV_ERR;
}
static int records(const char *base) {
    char path[4096];snprintf(path,sizeof path,"%s/lilly/sessions",base);
    DIR *d=opendir(path);if(!d)return 0;
    int n=0;struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')n++;
    closedir(d);return n;
}
int main(int argc,char **argv) {
    if(geteuid()!=0)return 77;
    int audit=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_AUDIT);
    if(audit<0 && (errno==EPERM||errno==EACCES)) {
        fputs("SKIP: host denies Linux-PAM audit netlink; run the PAM VM test\n",stderr);return 77;
    }
    if(audit>=0)close(audit);
    assert(argc==4);const char *module=argv[1],*base=argv[2],*conf=argv[3];
    assert(!mkdir(base,0755));assert(!mkdir(conf,0700));
    char path[4096];snprintf(path,sizeof path,"%s/lilly-test",conf);
    FILE *f=fopen(path,"w");assert(f);fprintf(f,"session required %s\n",module);assert(!fclose(f));
    struct pam_conv conv={conversation,NULL};
    struct passwd *pw=getpwuid(0);assert(pw);
    pam_handle_t *a=NULL,*b=NULL,*c=NULL;
    assert(pam_start_confdir("lilly-test",pw->pw_name,&conv,conf,&a)==PAM_SUCCESS);
    assert(pam_start_confdir("lilly-test",pw->pw_name,&conv,conf,&b)==PAM_SUCCESS);
    assert(pam_set_item(a,PAM_TTY,"tty1\nINJECTION=yes")==PAM_SUCCESS);
    int opened=pam_open_session(a,0);if(opened!=PAM_SUCCESS)fprintf(stderr,"open failed: %d %s\n",opened,pam_strerror(a,opened));
    if(opened!=PAM_SUCCESS)fprintf(stderr,"records=%d env=%s\n",records(base),pam_getenv(a,"XDG_SESSION_ID"));
    assert(opened==PAM_SUCCESS);
    assert(pam_open_session(b,0)==PAM_SUCCESS);
    assert(records(base)==2);
    const char *id=pam_getenv(a,"XDG_SESSION_ID");assert(id&&strlen(id)==32);
    snprintf(path,sizeof path,"%s/lilly/sessions/%s",base,id);
    FILE *record=fopen(path,"r");assert(record);char content[4096];size_t got=fread(content,1,sizeof content-1,record);content[got]=0;fclose(record);
    assert(strstr(content,"TTY=tty1%0aINJECTION%3dyes\n"));
    assert(!strstr(content,"\nINJECTION="));
    assert(strcmp(id,pam_getenv(b,"XDG_SESSION_ID")));
    snprintf(path,sizeof path,"%s/user/0",base);
    struct stat st;assert(!stat(path,&st)&&st.st_uid==0&&(st.st_mode&0777)==0700);
    assert(!strcmp(path,pam_getenv(a,"XDG_RUNTIME_DIR")));
    assert(pam_open_session(a,0)==PAM_SUCCESS); /* idempotent handle, no extra record */
    assert(records(base)==2);
    int closed=pam_close_session(a,0);if(closed!=PAM_SUCCESS)fprintf(stderr,"close failed: %d %s records=%d\n",closed,pam_strerror(a,closed),records(base));
    assert(closed==PAM_SUCCESS);assert(records(base)==1);
    assert(!stat(path,&st)); /* another session still owns runtime usage */
    assert(pam_end(a,PAM_SUCCESS)==PAM_SUCCESS);
    assert(pam_end(b,PAM_SUCCESS)==PAM_SUCCESS);assert(records(base)==0);
    /* Never follow a user/runtime directory symlink. */
    assert(!rmdir(path));assert(!symlink("/tmp",path));
    assert(pam_start_confdir("lilly-test",pw->pw_name,&conv,conf,&c)==PAM_SUCCESS);
    assert(pam_open_session(c,0)!=PAM_SUCCESS);assert(records(base)==0);
    assert(pam_end(c,PAM_SUCCESS)==PAM_SUCCESS);assert(!unlink(path));
    puts("PAM sessions: open, close, concurrent handles, environment, cleanup and symlink rejection passed");
    return 0;
}
