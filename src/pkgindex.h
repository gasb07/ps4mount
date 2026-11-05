/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Index of the PS4 pkgs of a drive, wherever they are. It only reads the drive. */
#pragma once

#include <limits.h>
#include <stdint.h>

#define PIDX_MAX_DEPTH 8    /* folder levels searched below each folder */
#define PIDX_MAX_PKGS 16384 /* pkgs kept per drive */

enum { PIDX_OTHER, PIDX_GAME, PIDX_UPDATE, PIDX_ADDON };

typedef struct {
    char path[PATH_MAX];
    uint64_t size;
    int64_t mtime;
    int kind;       /* PIDX_OTHER: not a fake PS4 pkg, or unreadable */
    int incomplete; /* smaller than its header says: still being copied, or cut */
    char title_id[10], content_id[37], ver[8];
    int seen;
} pidx_pkg_t;

typedef struct {
    pidx_pkg_t *pkgs; /* sorted by path after a scan */
    int count, cap;
    int skipped; /* pkgs past PIDX_MAX_PKGS in the last scan */
} pidx_t;

/* A game and what the drive has for it: indexes in pidx_t.pkgs, -1 for none. */
typedef struct {
    char title_id[10];
    int game, update;
    int copies; /* game pkgs not used: another one has a higher version, or comes first */
    int addons;
} pidx_game_t;

/* Fills in kind, incomplete and the IDs of the pkg at p->path. */
typedef void (*pidx_read_fn)(pidx_pkg_t *p);

/* Reads a pkg with pkg.c. An unreadable one is left as PIDX_OTHER. */
void pidx_read_pkg(pidx_pkg_t *p);

/* Looks for pkgs in the folders (relative to root, "" is root itself) and in root/ps4. Only
 * pkgs that are new, or whose size or time changed, are read. How many pkgs the index has. */
int pidx_scan(pidx_t *idx, const char *root, const char (*folders)[128], int nfolders, pidx_read_fn read);
void pidx_free(pidx_t *idx);

/* Groups the pkgs per title, sorted by title ID: the highest version is used, and between
 * equal versions the first by path. How many titles were put in out. */
int pidx_group(const pidx_t *idx, pidx_game_t *out, int max);
