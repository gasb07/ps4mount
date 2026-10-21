/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Log file size limit. */
#include "logfile.h"

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int log_trim(const char *path, off_t max, off_t keep) {
    struct stat st;
    if (stat(path, &st) || st.st_size <= max)
        return 0;
    size_t n = (size_t)(keep < st.st_size ? keep : st.st_size);
    char *buf = malloc(n);
    int in = open(path, O_RDONLY);
    ssize_t got = buf && in >= 0 ? pread(in, buf, n, st.st_size - (off_t)n) : -1;
    if (in >= 0)
        close(in);
    if (got != (ssize_t)n) {
        free(buf);
        return -1;
    }
    char *nl = memchr(buf, '\n', n);
    size_t skip = nl ? (size_t)(nl - buf) + 1 : 0;
    /* written aside and renamed over the log, so a failure never loses it */
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666), rc = out < 0;
    if (!rc && write(out, buf + skip, n - skip) != (ssize_t)(n - skip))
        rc = 1;
    if (out >= 0)
        close(out);
    free(buf);
    if (!rc && rename(tmp, path))
        rc = 1;
    if (rc)
        unlink(tmp);
    return rc ? -1 : 1;
}
