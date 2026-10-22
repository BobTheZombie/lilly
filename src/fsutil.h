/* SPDX-License-Identifier: MIT */
#ifndef LILLY_FSUTIL_H
#define LILLY_FSUTIL_H
#include <stddef.h>
#include <sys/types.h>
int fs_root(const char *path);
int fs_dir(int root, const char *path, int create);
int fs_open(int root, const char *path, int flags);
int fs_write(int root, const char *path, const void *data, size_t size, mode_t mode, int replace);
int fs_copy(int source, int root, const char *path, int replace);
int fs_remove(int root, const char *path);
int fs_name(const char *name);
char *fs_read(int fd, size_t limit);
#endif
