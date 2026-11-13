/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Tests of the repeated kernel log line rules. Build and run with: bash test/run.sh */
#include <stdio.h>
#include <string.h>

#include "klogdup.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

static void test_first_written(void) {
    printf("first written\n");
    kdup_t d = {0};
    char n[256];
    CHECK(kdup_line(&d, "a", 100, n, sizeof(n)) == KDUP_WRITE);
    CHECK(kdup_line(&d, "b", 100, n, sizeof(n)) == KDUP_WRITE);
    CHECK(kdup_flush(&d, 100, n, sizeof(n)) == 0);
}

static void test_group_repeats(void) {
    printf("group of 4 repeating\n");
    kdup_t d = {0};
    char n[256];
    const char *g[] = {"ERROR: sceSblPfsClearKey(510) ret -2", "[0]pfs_unmount() line=3170 umount error = -2",
                       "INFO: skip unmount", "WARNING: force unmount"};
    int written = 0;
    for (int r = 0; r < 100; r++)
        for (int i = 0; i < 4; i++)
            written += kdup_line(&d, g[i], 100 + r / 10, n, sizeof(n)) & KDUP_WRITE;
    CHECK(written == 4);
    CHECK(kdup_flush(&d, 159, n, sizeof(n)) == 0); /* window not over */
    int notes = 0;
    while (kdup_flush(&d, 160, n, sizeof(n))) {
        notes++;
        CHECK(strstr(n, "repeated 99 times") != NULL);
    }
    CHECK(notes == 4);
    /* a loop that goes on: one count per window, the line itself is not written again */
    CHECK(kdup_line(&d, g[0], 161, n, sizeof(n)) == 0);
    CHECK(kdup_flush(&d, 220, n, sizeof(n)) == 1 && strstr(n, "repeated 1 times"));
}

static void test_no_note_without_repeat(void) {
    printf("expired without repeat\n");
    kdup_t d = {0};
    char n[256];
    kdup_line(&d, "once", 100, n, sizeof(n));
    CHECK(kdup_flush(&d, 1000, n, sizeof(n)) == 0);
    CHECK(kdup_line(&d, "once", 1000, n, sizeof(n)) == KDUP_WRITE);
}

static void test_pushed_out(void) {
    printf("pushed out of the table\n");
    kdup_t d = {0};
    char n[256], l[32];
    kdup_line(&d, "loop", 100, n, sizeof(n));
    kdup_line(&d, "loop", 100, n, sizeof(n));
    int noted = 0;
    for (int i = 0; i < KDUP_SLOTS; i++) {
        snprintf(l, sizeof(l), "other %d", i);
        int r = kdup_line(&d, l, 101, n, sizeof(n));
        CHECK(r & KDUP_WRITE);
        if (r & KDUP_NOTE) {
            noted++;
            CHECK(strstr(n, "repeated 1 times") && strstr(n, "loop"));
        }
    }
    CHECK(noted == 1);
}

static void test_long_line(void) {
    printf("long line\n");
    kdup_t d = {0};
    char n[64], l[600];
    memset(l, 'x', sizeof(l) - 1);
    l[sizeof(l) - 1] = 0;
    kdup_line(&d, l, 100, n, sizeof(n));
    kdup_line(&d, l, 100, n, sizeof(n));
    CHECK(kdup_flush(&d, 200, n, sizeof(n)) == 1);
    CHECK(strlen(n) < sizeof(n));
}

int main(void) {
    test_first_written();
    test_group_repeats();
    test_no_note_without_repeat();
    test_pushed_out();
    test_long_line();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
