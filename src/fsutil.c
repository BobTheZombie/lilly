/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "fsutil.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

int fs_name(const char *s) {
    if (!*s || !strcmp(s,".") || strlen(s)>200 || strstr(s,"..")) return 0;
    for (;*s;s++) if (!isalnum((unsigned char)*s) && !strchr("_.@-",*s)) return 0;
    return 1;
}
int fs_root(const char *path) { return open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW); }
int fs_dir(int root,const char *path,int create) {
    while (*path=='/') path++;
    if (strlen(path)>=PATH_MAX) { errno=ENAMETOOLONG; return -1; }
    char copy[PATH_MAX]; strcpy(copy,path);
    int fd=fcntl(root,F_DUPFD_CLOEXEC,3); if(fd<0) return -1;
    char *save;
    for(char *s=strtok_r(copy,"/",&save);s;s=strtok_r(NULL,"/",&save)) {
        if(!fs_name(s)) {close(fd);errno=EINVAL;return -1;}
        if(create && mkdirat(fd,s,0755) && errno!=EEXIST) {int e=errno;close(fd);errno=e;return -1;}
        int next=openat(fd,s,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        int e=errno; close(fd); fd=next;
        if(fd<0) {errno=e;return -1;}
    }
    return fd;
}
static int parent(int root,const char *path,int create,char name[201]) {
    if(!*path || strlen(path)>=PATH_MAX) {errno=EINVAL;return -1;}
    char copy[PATH_MAX];strcpy(copy,path);
    char *last=strrchr(copy,'/');
    const char *base=last?last+1:copy;
    if(!fs_name(base)) {errno=EINVAL;return -1;}
    strcpy(name,base);
    if(last) *last=0;
    return fs_dir(root,last?copy:"",create);
}
int fs_open(int root,const char *path,int flags) {
    char name[201]; int d=parent(root,path,0,name); if(d<0)return -1;
    int fd=openat(d,name,flags|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    int e=errno;close(d);
    if(fd>=0) {
        struct stat st;
        if(fstat(fd,&st) || !S_ISREG(st.st_mode)) {close(fd);errno=EINVAL;return -1;}
    }
    errno=e;return fd;
}
char *fs_read(int fd,size_t limit) {
    char *out=calloc(1,limit+1);if(!out)return NULL;
    size_t n=0;
    for(;;) {
        ssize_t got=read(fd,out+n,limit-n);
        if(got<0 && errno==EINTR)continue;
        if(got<0) {free(out);return NULL;}
        if(!got) break;
        n+=(size_t)got;
        if(n==limit) {free(out);errno=EFBIG;return NULL;}
    }
    if(memchr(out,0,n)) {free(out);errno=EINVAL;return NULL;}
    return out;
}
static int transfer(int src,int dst,const void *data,size_t size) {
    char buf[65536];
    for(;;) {
        ssize_t n;
        if(src>=0) {do{n=read(src,buf,sizeof buf);}while(n<0&&errno==EINTR);if(n<0)return -1;if(!n)break;}
        else {n=(ssize_t)(size>sizeof buf?sizeof buf:size);if(!n)break;memcpy(buf,data,(size_t)n);data=(const char *)data+n;size-=(size_t)n;}
        size_t off=0;
        while(off<(size_t)n) {ssize_t written=write(dst,buf+off,(size_t)n-off);if(written<0&&errno==EINTR)continue;if(written<=0)return -1;off+=(size_t)written;}
    }
    return 0;
}
static int publish(int src,int root,const char *path,const void *data,size_t size,mode_t mode,int replace) {
    char name[201];int d=parent(root,path,1,name);if(d<0)return -1;
    struct stat st;
    if(!fstatat(d,name,&st,AT_SYMLINK_NOFOLLOW) && (!S_ISREG(st.st_mode)||!replace)) {close(d);errno=EEXIST;return -1;}
    unsigned long nonce;
    if(getrandom(&nonce,sizeof nonce,0)!=sizeof nonce) {close(d);return -1;}
    char tmp[64];snprintf(tmp,sizeof tmp,".lilly-%ld-%lx",(long)getpid(),nonce);
    int fd=openat(d,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);
    if(fd<0){close(d);return -1;}
    int r=transfer(src,fd,data,size);
    if(!r)r=fsync(fd);
    int e=errno;if(close(fd)&&!r){r=-1;e=errno;}
    if(!r) {
        if(replace)r=renameat(d,tmp,d,name);
        else r=renameat2(d,tmp,d,name,RENAME_NOREPLACE);
        e=errno;
    }
    if(r)unlinkat(d,tmp,0);
    /* FAT does not support directory fsync on every implementation. */
    if(!r && fsync(d) && errno!=EINVAL && errno!=EROFS){r=-1;e=errno;}
    close(d);errno=e;return r;
}
int fs_write(int root,const char *path,const void *data,size_t size,mode_t mode,int replace) {return publish(-1,root,path,data,size,mode,replace);}
int fs_copy(int source,int root,const char *path,int replace) {return publish(source,root,path,NULL,0,0644,replace);}
int fs_remove(int root,const char *path) {
    char name[201];int d=parent(root,path,0,name);if(d<0)return -1;
    struct stat st;
    if(fstatat(d,name,&st,AT_SYMLINK_NOFOLLOW)) {int e=errno;close(d);errno=e;return -1;}
    if(!S_ISREG(st.st_mode)) {close(d);errno=EINVAL;return -1;}
    int r=unlinkat(d,name,0);int e=errno;if(!r)fsync(d);close(d);errno=e;return r;
}
