/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
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
    unsigned starts;
    pid_t pid;
    long long retry;
};
static struct unit units[MAX_UNITS];
static int count, order[MAX_UNITS], norder;
static bool stopping;
static long long deadline;
static int failed;
static void die(const char *s) { fprintf(stderr, "lilly: %s\n", s); exit(1); }
static void syserr(const char *s) { perror(s); exit(1); }
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
    if (!*s || strlen(s) > 200 || strstr(s, "..")) return false;
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
static void parse(const char *dir, const char *name) {
    if (count == MAX_UNITS) die("too many units");
    char path[PATH_MAX];
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path) die("unit path too long");
    FILE *f = fopen(path, "re");
    if (!f) syserr(path);
    struct unit *u = &units[count++];
    u->name = dupstr(name);
    char line[MAX_LINE], section[32] = "";
    bool type_seen = false, restart_seen = false;
    while (fgets(line, sizeof line, f)) {
        if (!strchr(line, '\n') && !feof(f)) die("unit line too long");
        char *s = trim(line);
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            if (strcmp(s, "[Unit]") && strcmp(s, "[Service]")) die("unsupported unit section");
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
            else die("unsupported Unit directive");
        } else if (!strcmp(section, "[Service]")) {
            if (!strcmp(key, "ExecStart")) set(&u->exec, value);
            else if (!strcmp(key, "Type")) {
                if (type_seen) die("duplicate Type");
                type_seen = true;
                if (strcmp(value, "simple") && strcmp(value, "oneshot")) die("unsupported service Type");
                u->oneshot = !strcmp(value, "oneshot");
            } else if (!strcmp(key, "Restart")) {
                if (restart_seen) die("duplicate Restart");
                restart_seen = true;
                if (strcmp(value, "no") && strcmp(value, "on-failure")) die("unsupported Restart policy");
                u->restart = !strcmp(value, "on-failure");
            } else die("unsupported Service directive");
        } else die("directive outside section");
    }
    if (ferror(f)) syserr(path);
    fclose(f);
    if (!u->exec) die("service missing ExecStart");
    if (u->oneshot && u->restart) die("oneshot restart is not supported");
    command(u);
}
static void load(const char *dir) {
    struct dirent **entries;
    int n = scandir(dir, &entries, NULL, alphasort);
    if (n < 0) syserr(dir);
    for (int i = 0; i < n; i++) {
        char *name = entries[i]->d_name;
        size_t len = strlen(name);
        if (len > 8 && !strcmp(name + len - 8, ".service")) {
            if (!valid_name(name)) die("invalid service filename");
            parse(dir, name);
        }
        free(entries[i]);
    }
    free(entries);
    if (!count) die("no service units");
}
static int deps(const char *list, int out[MAX_DEPS], bool missing_ok) {
    if (!list) return 0;
    char *copy = dupstr(list), *save;
    int n = 0;
    for (char *t = strtok_r(copy, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        if (!valid_name(t)) die("invalid dependency name");
        int i = find(t);
        if (i < 0) { if (missing_ok) continue; fprintf(stderr, "lilly: missing requirement %s\n", t); exit(1); }
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
/* Requires pulls a unit in; After orders it. This preserves the distinction
 * used by systemd rather than implicitly treating Requires as After. */
static bool ready(struct unit *u) {
    int d[MAX_DEPS], n = deps(u->requires, d, false);
    for (int j = 0; j < n; j++) if (units[d[j]].failed) { u->failed = true; failed = 1; return false; }
    n = deps(u->after, d, true);
    for (int j = 0; j < n; j++) if (units[d[j]].selected) {
        struct unit *p = &units[d[j]];
        if (p->failed) { u->failed = true; failed = 1; return false; }
        if (p->oneshot ? !p->done : !p->pid && !p->done) return false;
    }
    return true;
}
static void start(struct unit *u, const sigset_t *old) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC)) syserr("pipe2");
    pid_t pid = fork();
    if (pid < 0) syserr("fork");
    if (!pid) {
        close(pipefd[0]);
        int e = 0;
        if (setpgid(0, 0) || sigprocmask(SIG_SETMASK, old, NULL)) e = errno;
        if (!e) { execv(u->argv[0], u->argv); e = errno; }
        ssize_t written = write(pipefd[1], &e, sizeof e);
        if (written != sizeof e) _exit(126);
        _exit(127);
    }
    close(pipefd[1]);
    int e = 0;
    ssize_t r;
    do { r = read(pipefd[0], &e, sizeof e); } while (r < 0 && errno == EINTR);
    close(pipefd[0]);
    u->starts++;
    u->pid = pid;
    if (r != 0) { fprintf(stderr, "lilly: %s exec failed: %s\n", u->name, strerror(e ? e : EIO)); u->failed = true; failed = 1; }
    else fprintf(stderr, "lilly: started %s pid=%ld\n", u->name, (long)pid);
}
static void stop_all(void) {
    stopping = true;
    deadline = now() + 3000;
    for (int j = norder - 1; j >= 0; j--) if (units[order[j]].pid > 0) kill(-units[order[j]].pid, SIGTERM);
}
static int run(void) {
    if (getpid() == 1) die("development supervisor is not yet a bootable PID 1; see docs/ROADMAP.md");
    sigset_t mask, old;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD); sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGINT);
    if (sigprocmask(SIG_BLOCK, &mask, &old)) syserr("sigprocmask");
    sigset_t empty; sigemptyset(&empty);
    if (prctl(PR_SET_CHILD_SUBREAPER, 1)) syserr("subreaper");
    int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0) syserr("signalfd");
    for (;;) {
        bool pending = false;
        if (!stopping) for (int j = 0; j < norder; j++) {
            struct unit *u = &units[order[j]];
            if (!u->pid && !u->done && !u->failed) {
                if (u->retry <= now() && ready(u)) start(u, &empty);
                if (!u->failed) pending = true;
            }
        }
        bool live = false;
        for (int j = 0; j < norder; j++) if (units[order[j]].pid) live = true;
        if (!live && (!pending || stopping)) break;
        struct pollfd p = { .fd = fd, .events = POLLIN };
        if (poll(&p, 1, 100) < 0 && errno != EINTR) syserr("poll");
        struct signalfd_siginfo info;
        while (read(fd, &info, sizeof info) == sizeof info)
            if ((info.ssi_signo == SIGTERM || info.ssi_signo == SIGINT) && !stopping) stop_all();
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int j = 0; j < norder; j++) {
                struct unit *u = &units[order[j]];
                if (u->pid != pid) continue;
                /* Services must stay in their process group: remove leftover children. */
                kill(-pid, SIGKILL);
                u->pid = 0;
                bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                fprintf(stderr, "lilly: exited %s status=%d\n", u->name, status);
                if (!stopping && !ok && !u->failed && u->restart && u->starts < 5) u->retry = now() + 250;
                else if (!stopping && (!ok || u->failed)) { u->failed = true; failed = 1; }
                else u->done = true;
            }
        }
        if (stopping && now() >= deadline)
            for (int j = 0; j < norder; j++) if (units[order[j]].pid) kill(-units[order[j]].pid, SIGKILL);
    }
    close(fd);
    sigprocmask(SIG_SETMASK, &old, NULL);
    return failed;
}
static void cleanup(void) {
    for (int i = 0; i < count; i++) {
        free(units[i].name); free(units[i].exec); free(units[i].after); free(units[i].requires);
        for (int j = 0; units[i].argv[j]; j++) free(units[i].argv[j]);
    }
}
int main(int argc, char **argv) {
    if (atexit(cleanup)) die("atexit failed");
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("lilly 0.1.0-dev"); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        puts("Usage: lilly {verify|plan|run} UNIT_DIRECTORY [SERVICE]\nDevelopment service supervisor; PID 1 boot support is not implemented."); return 0;
    }
    if (argc < 3 || argc > 4) die("usage: lilly {verify|plan|run} UNIT_DIRECTORY [SERVICE]");
    bool verify = !strcmp(argv[1], "verify"), plan = !strcmp(argv[1], "plan");
    if (!verify && !plan && strcmp(argv[1], "run")) die("unknown command");
    load(argv[2]);
    if (argc == 4) { int i = find(argv[3]); if (i < 0) die("unknown requested service"); select_unit(i); }
    else for (int i = 0; i < count; i++) select_unit(i);
    for (int i = 0; i < count; i++) if (units[i].selected) visit(i);
    if (verify) { puts("units verified"); return 0; }
    if (plan) { for (int i = 0; i < norder; i++) puts(units[order[i]].name); return 0; }
    return run();
}
