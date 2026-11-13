/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Origin records of moved pkgs. */
#include "origin.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* Appends one line and forces it to disk: a record lost in a power cut would leave a pkg
 * that cannot be moved back to its name. */
static int append_line(const char *path, const char *line) {
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return -1;
    size_t len = strlen(line);
    int rc = write(fd, line, len) == (ssize_t)len ? 0 : -1;
    fsync(fd);
    close(fd);
    return rc;
}

int origin_add(const char *game_dir, const char *rel, const char *orig) {
    char path[1024], line[2048];
    if (strchr(rel, '\t') || strchr(rel, '\n') || strchr(orig, '\t') || strchr(orig, '\n'))
        return -1;
    snprintf(path, sizeof(path), "%s/" ORIGIN_FILE, game_dir);
    snprintf(line, sizeof(line), "%s\t%s\n", rel, orig);
    return append_line(path, line);
}

int origin_find(const char *game_dir, const char *rel, char *orig, size_t n) {
    char path[1024], line[2048];
    snprintf(path, sizeof(path), "%s/" ORIGIN_FILE, game_dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    size_t rl = strlen(rel);
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, rel, rl) && line[rl] == '\t' && line[rl + 1]) {
            snprintf(orig, n, "%s", line + rl + 1);
            found = 1;
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

int origin_free_path(const char *root, const char *orig, char *out, size_t n) {
    struct stat st;
    snprintf(out, n, "%s/%s", root, orig);
    if (stat(out, &st))
        return 0;
    const char *name = strrchr(orig, '/');
    name = name ? name + 1 : orig;
    const char *dot = strrchr(name, '.');
    size_t base = dot && dot != name ? (size_t)(dot - orig) : strlen(orig);
    for (int i = 1; i < 100; i++) {
        snprintf(out, n, "%s/%.*s (%d)%s", root, (int)base, orig, i, orig + base);
        if (stat(out, &st))
            return 0;
    }
    return -1;
}
