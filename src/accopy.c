/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07 */
#include "accopy.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void state_path(char *out, size_t n, const char *dir, const char *cid, const char *ext) {
    snprintf(out, n, "%s/%.36s.%s", dir, cid, ext);
}

int accopy_mark(const char *dir, const char *cid, const char *pkg_path) {
    char p[160];
    state_path(p, sizeof(p), dir, cid, "accopy");
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;
    size_t n = strlen(pkg_path);
    int rc = write(fd, pkg_path, n) == (ssize_t)n ? 0 : -1;
    close(fd);
    return rc;
}

int accopy_marked(const char *dir, const char *cid) {
    char p[160];
    state_path(p, sizeof(p), dir, cid, "accopy");
    return access(p, F_OK) == 0;
}

void accopy_unmark(const char *dir, const char *cid) {
    char p[160];
    state_path(p, sizeof(p), dir, cid, "accopy");
    unlink(p);
}

int acfail_add(const char *dir, const char *cid, const char *reason) {
    char p[160];
    state_path(p, sizeof(p), dir, cid, "acfail");
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return -1;
    char line[128];
    int n = snprintf(line, sizeof(line), "%.120s\n", reason);
    int rc = write(fd, line, (size_t)n) == n ? 0 : -1;
    close(fd);
    return rc ? -1 : acfail_get(dir, cid, NULL, 0);
}

int acfail_get(const char *dir, const char *cid, char *reason, size_t cap) {
    char p[160], buf[1024];
    if (reason && cap)
        reason[0] = 0;
    state_path(p, sizeof(p), dir, cid, "acfail");
    int fd = open(p, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return 0;
    buf[n] = 0;
    int count = 0;
    char *line = buf, *nl;
    while ((nl = strchr(line, '\n'))) {
        *nl = 0;
        count++;
        if (reason && cap)
            snprintf(reason, cap, "%s", line);
        line = nl + 1;
    }
    return count;
}

void acfail_clear(const char *dir, const char *cid) {
    char p[160];
    state_path(p, sizeof(p), dir, cid, "acfail");
    unlink(p);
}

int accopy_space_ok(uint64_t free_bytes, uint64_t pkg_size) { return free_bytes > pkg_size + (1ULL << 30); }

void acwait_start(acwait_t *w, time_t now) {
    w->last = 0;
    w->start = w->grew = now;
}

int acwait_step(acwait_t *w, uint64_t cur, uint64_t want, time_t now) {
    if (cur >= want)
        return ACW_DONE;
    if (cur > w->last || !cur) {
        /* nothing written here yet: the system copies elsewhere and moves ac.pkg in at the end */
        w->last = cur;
        w->grew = now;
    }
    if (now - w->start > ACW_TIMEOUT_S)
        return ACW_TIMEOUT;
    if (now - w->grew > ACW_STALL_S)
        return ACW_STALL;
    return ACW_WAIT;
}

int accopy_decide(const acin_t *in) {
    if (in->busy)
        return AC_NONE;
    if (in->store)
        return AC_STORE;
    if (!in->on)
        return in->marked ? AC_UNINSTALL : AC_NONE;
    if (!in->marked)
        return in->fails >= ACFAIL_MAX ? AC_PROBLEM : AC_INSTALL;
    if (!in->files)
        return in->rereg ? AC_REINSTALL : AC_TURN_OFF;
    return in->playgo ? AC_NONE : AC_REINSTALL;
}

int accopy_files_done(const char *dir, uint64_t size) {
    char p[PATH_MAX];
    struct stat st;
    snprintf(p, sizeof(p), "%s/ac.pkg", dir);
    if (stat(p, &st) || (uint64_t)st.st_size < size)
        return 0;
    snprintf(p, sizeof(p), "%s/ac.json", dir);
    if (stat(p, &st))
        return 0;
    snprintf(p, sizeof(p), "%s/ac.pbm", dir);
    return !stat(p, &st);
}
