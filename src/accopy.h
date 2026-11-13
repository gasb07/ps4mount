/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Add-ons copied to internal storage by the system, in copy mode: the state files kept for
 * them, the free space and copy progress rules and the decision taken for each add-on. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* <dir>/<content id>.accopy: the add-on was installed by PS4Mount (holds the pkg path).
 * Only add-ons with this file are ever uninstalled by PS4Mount. */
int accopy_mark(const char *dir, const char *cid, const char *pkg_path);
int accopy_marked(const char *dir, const char *cid);
void accopy_unmark(const char *dir, const char *cid);

/* <dir>/<content id>.acfail: one line per failed install, the reason. */
#define ACFAIL_MAX 3
int acfail_add(const char *dir, const char *cid, const char *reason); /* new count, -1 on error */
int acfail_get(const char *dir, const char *cid, char *reason, size_t cap); /* count, last reason */
void acfail_clear(const char *dir, const char *cid);

/* Free space on internal storage must exceed the pkg size plus 1 GiB. */
int accopy_space_ok(uint64_t free_bytes, uint64_t pkg_size);

/* Copy progress, checked every few seconds against the size of the copied pkg. */
#define ACW_TIMEOUT_S 1800
#define ACW_STALL_S 120
enum { ACW_WAIT, ACW_DONE, ACW_TIMEOUT, ACW_STALL };
typedef struct {
    uint64_t last;
    time_t start, grew;
} acwait_t;
void acwait_start(acwait_t *w, time_t now);
int acwait_step(acwait_t *w, uint64_t cur, uint64_t want, time_t now);
/* The copy is over without klog: dir (/user/addcont/<TID>/<LABEL>) holds ac.pkg at full
 * size, ac.json and ac.pbm. */
int accopy_files_done(const char *dir, uint64_t size);

/* What is known of one add-on. files: /user/addcont/<TID>/<LABEL>/ac.pkg exists. store:
 * files there that PS4Mount did not install. playgo: /user/playgo/addcont/<CID> exists.
 * busy: being installed now. rereg: the game was registered again after the copy (the
 * system may have removed it then). */
typedef struct {
    int on, marked, files, store, playgo, fails, busy, rereg;
} acin_t;
enum { AC_NONE, AC_INSTALL, AC_UNINSTALL, AC_TURN_OFF, AC_REINSTALL, AC_PROBLEM, AC_STORE };
int accopy_decide(const acin_t *in);
