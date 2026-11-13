/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07 */
#include "klogdup.h"

#include <stdio.h>
#include <string.h>

static uint32_t hash_of(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static void note_of(const kdup_entry_t *e, char *note, size_t cap) {
    snprintf(note, cap, "repeated %u times: %s", e->count, e->text);
}

static void fill(kdup_entry_t *e, uint32_t h, const char *line, time_t now) {
    e->hash = h;
    e->count = 0;
    e->since = now;
    snprintf(e->text, sizeof(e->text), "%s", line);
}

int kdup_line(kdup_t *d, const char *line, time_t now, char *note, size_t cap) {
    uint32_t h = hash_of(line);
    for (int i = 0; i < d->used; i++) {
        kdup_entry_t *e = &d->e[i];
        if (e->hash != h || strncmp(e->text, line, sizeof(e->text) - 1))
            continue;
        if (now - e->since < KDUP_WINDOW_S) {
            e->count++;
            return 0;
        }
        int r = KDUP_WRITE;
        if (e->count) {
            note_of(e, note, cap);
            r |= KDUP_NOTE;
        }
        fill(e, h, line, now);
        return r;
    }
    int r = KDUP_WRITE;
    kdup_entry_t *e;
    if (d->used < KDUP_SLOTS) {
        e = &d->e[d->used++];
    } else {
        e = &d->e[d->next];
        d->next = (d->next + 1) % KDUP_SLOTS;
        if (e->count) {
            note_of(e, note, cap);
            r |= KDUP_NOTE;
        }
    }
    fill(e, h, line, now);
    return r;
}

int kdup_flush(kdup_t *d, time_t now, char *note, size_t cap) {
    for (int i = 0; i < d->used; i++) {
        kdup_entry_t *e = &d->e[i];
        if (e->count && now - e->since >= KDUP_WINDOW_S) {
            note_of(e, note, cap);
            e->count = 0;
            e->since = now;
            return 1;
        }
    }
    return 0;
}
