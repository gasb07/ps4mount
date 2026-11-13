/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Repeated kernel log lines: a line seen again within KDUP_WINDOW_S is not written again,
 * it is counted, and the count is written once for the window. Groups of up to KDUP_SLOTS
 * lines that repeat together are caught too. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define KDUP_SLOTS 16
#define KDUP_WINDOW_S 60
#define KDUP_TEXT 120

typedef struct {
    uint32_t hash;
    unsigned count; /* repeats not written */
    time_t since;   /* window start */
    char text[KDUP_TEXT];
} kdup_entry_t;

typedef struct {
    kdup_entry_t e[KDUP_SLOTS];
    int used, next;
} kdup_t;

enum { KDUP_WRITE = 1, KDUP_NOTE = 2 };

/* What to do with a line: KDUP_WRITE to write it, KDUP_NOTE when note holds the count of a
 * line it pushed out of the table (write the note first). 0: a repeat, skip it. */
int kdup_line(kdup_t *d, const char *line, time_t now, char *note, size_t cap);

/* One count whose window ended, in note: 1, 0 when none. Call until 0. */
int kdup_flush(kdup_t *d, time_t now, char *note, size_t cap);
