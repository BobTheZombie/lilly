/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "fsutil.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2||strcmp(argv[1],"list")) {puts("Usage: lilly-sessions list");return argc==2&&!strcmp(argv[1],"--help")?0:2;}
    int run=fs_root("/run");if(run<0){perror("/run");return 1;}
    int records=fs_dir(run,"lilly/sessions",0);close(run);
    if(records<0){perror("Lilly sessions");return 1;}
    DIR *dir=fdopendir(records);if(!dir){close(records);return 1;}
    struct dirent *entry;int result=0;
    while((entry=readdir(dir))) {
        if(entry->d_name[0]=='.')continue;
        if(strlen(entry->d_name)!=32 || strspn(entry->d_name,"0123456789abcdef")!=32)continue;
        int fd=fs_open(records,entry->d_name,O_RDONLY);if(fd<0){result=1;continue;}
        char *data=fs_read(fd,4096);close(fd);if(!data){result=1;continue;}
        /* Values are percent encoded by the module, never terminal escapes. */
        if(strspn(data,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%=\n/@_.:-")!=strlen(data))result=1;
        else {fputs(data,stdout);putchar('\n');}
        free(data);
    }
    closedir(dir);return result;
}
