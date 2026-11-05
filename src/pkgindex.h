/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Index of the PS4 pkgs of a drive, wherever they are. It only reads the drive. */
#pragma once

#include <limits.h>
#include <stddef.h>
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
    char name[128];    /* TITLE */
    uint64_t pkg_size; /* from the header */
    uint8_t digest[32];
    int data;    /* add-ons: has content, not only a license */
    int seen;
    int ignored; /* set by the caller after a scan: left out of the games */
    int claimed; /* set by the caller: an add-on some game uses */
} pidx_pkg_t;

typedef struct {
    pidx_pkg_t *pkgs; /* sorted by path after a scan */
    int count, cap;
    int skipped; /* pkgs past PIDX_MAX_PKGS in the last scan */
    char root[32];
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

/* The pkg can be used: readable, complete, not left out and not an update kept aside. */
int pidx_usable(const pidx_t *idx, const pidx_pkg_t *p);

/* The pkgs to use for tid. A game organized by an earlier version (legacy_app) stays as it is;
 * otherwise the index gives it. The update is the version the player chose (want, NULL or ""
 * for none) when it is there, else the highest version, legacy_patch when no other one is
 * higher. app and patch always get a path, the legacy one when nothing else applies. 0 when
 * the game exists, -1 when it does not. */
int pidx_resolve(const pidx_t *idx, const char *want, const char *tid, const char *legacy_app, const char *legacy_patch,
                 char *app, char *patch, size_t n);

/* The updates of tid that can be used: indexes in pidx_t.pkgs. How many were put in out. */
int pidx_updates(const pidx_t *idx, const char *tid, int *out, int max);
