/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Tests of the add-on copy rules: state files, free space, copy progress and the decision
 * for each add-on. Build and run with: bash test/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "accopy.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

#define CID "EP0700-CUSA32836_00-NUSANCHAKAWAKIKR"

static char g_dir[64];

static void test_mark(void) {
    printf("mark\n");
    CHECK(!accopy_marked(g_dir, CID));
    CHECK(accopy_mark(g_dir, CID, "/mnt/usb0/ps4/CUSA32836/addcont/CUSA32836/NUSANCHAKAWAKIKR/ac.pkg") == 0);
    CHECK(accopy_marked(g_dir, CID));
    accopy_unmark(g_dir, CID);
    CHECK(!accopy_marked(g_dir, CID));
}

static void test_fail_count(void) {
    printf("fail count\n");
    char why[64] = "x";
    CHECK(acfail_get(g_dir, CID, why, sizeof(why)) == 0);
    CHECK(why[0] == 0);
    CHECK(acfail_add(g_dir, CID, "copy timed out") == 1);
    CHECK(acfail_add(g_dir, CID, "install call 0x80a30004") == 2);
    CHECK(acfail_add(g_dir, CID, "copy stopped") == 3);
    CHECK(acfail_get(g_dir, CID, why, sizeof(why)) == 3);
    CHECK(!strcmp(why, "copy stopped"));
    acfail_clear(g_dir, CID);
    CHECK(acfail_get(g_dir, CID, why, sizeof(why)) == 0);
}

static void test_space(void) {
    printf("space\n");
    CHECK(accopy_space_ok(2ULL << 30, 1ULL << 30) == 0);
    CHECK(accopy_space_ok((2ULL << 30) + 1, 1ULL << 30) == 1);
    CHECK(accopy_space_ok(0, 0) == 0);
}

static void test_progress_done(void) {
    printf("progress done\n");
    acwait_t w;
    acwait_start(&w, 1000);
    CHECK(acwait_step(&w, 10, 100, 1002) == ACW_WAIT);
    CHECK(acwait_step(&w, 100, 100, 1004) == ACW_DONE);
}

static void test_progress_stall(void) {
    printf("progress stall\n");
    acwait_t w;
    acwait_start(&w, 0);
    CHECK(acwait_step(&w, 5, 100, 0) == ACW_WAIT);
    CHECK(acwait_step(&w, 5, 100, 121) == ACW_STALL);
    acwait_start(&w, 0);
    CHECK(acwait_step(&w, 5, 100, 0) == ACW_WAIT);
    CHECK(acwait_step(&w, 6, 100, 100) == ACW_WAIT);
    CHECK(acwait_step(&w, 6, 100, 200) == ACW_WAIT);
}

static void test_progress_timeout(void) {
    printf("progress timeout\n");
    acwait_t w;
    acwait_start(&w, 0);
    int r = ACW_WAIT;
    for (int t = 0; t <= 1800; t += 60)
        r = acwait_step(&w, (uint64_t)t + 1, 1ULL << 40, t);
    CHECK(r == ACW_WAIT);
    CHECK(acwait_step(&w, 5000, 1ULL << 40, 1801) == ACW_TIMEOUT);
}

/* the system copies elsewhere and moves ac.pkg in at the end: nothing written yet is not a stall */
static void test_progress_not_here_yet(void) {
    printf("progress not here yet\n");
    acwait_t w;
    acwait_start(&w, 0);
    CHECK(acwait_step(&w, 0, 100, 0) == ACW_WAIT);
    CHECK(acwait_step(&w, 0, 100, 600) == ACW_WAIT);
    CHECK(acwait_step(&w, 5, 100, 602) == ACW_WAIT);
    CHECK(acwait_step(&w, 5, 100, 723) == ACW_STALL);
    acwait_start(&w, 0);
    CHECK(acwait_step(&w, 0, 100, 1801) == ACW_TIMEOUT);
}

/* the files the system leaves when the copy is over, without klog */
static void test_files_done(void) {
    printf("files done\n");
    char d[96], f[128];
    snprintf(d, sizeof(d), "%s/LABEL", g_dir);
    CHECK(!accopy_files_done(d, 4));
    mkdir(d, 0777);
    snprintf(f, sizeof(f), "%s/ac.pkg", d);
    FILE *fp = fopen(f, "w");
    fputs("abc", fp);
    fclose(fp);
    snprintf(f, sizeof(f), "%s/ac.json", d);
    fclose(fopen(f, "w"));
    snprintf(f, sizeof(f), "%s/ac.pbm", d);
    fclose(fopen(f, "w"));
    CHECK(!accopy_files_done(d, 4)); /* ac.pkg short */
    snprintf(f, sizeof(f), "%s/ac.pkg", d);
    fp = fopen(f, "a");
    fputs("d", fp);
    fclose(fp);
    CHECK(accopy_files_done(d, 4));
    snprintf(f, sizeof(f), "%s/ac.pbm", d);
    unlink(f);
    CHECK(!accopy_files_done(d, 4));
}

static int decide(int on, int store, int marked, int files, int playgo, int fails, int busy) {
    acin_t in = {on, marked, files, store, playgo, fails, busy, 0};
    return accopy_decide(&in);
}

/* files gone after a registration PS4Mount ran: installed again, not turned off */
static void test_state_rereg(void) {
    printf("state rereg\n");
    acin_t in = {1, 1, 0, 0, 1, 0, 0, 1};
    CHECK(accopy_decide(&in) == AC_REINSTALL);
    in.rereg = 0;
    CHECK(accopy_decide(&in) == AC_TURN_OFF);
}

static void test_state_store(void) {
    printf("state store\n");
    CHECK(decide(0, 1, 0, 1, 1, 0, 0) == AC_STORE);
    CHECK(decide(1, 1, 0, 1, 1, 0, 0) == AC_STORE);
}

static void test_state_playgo_lost(void) {
    printf("state playgo lost\n");
    CHECK(decide(1, 0, 1, 1, 0, 0, 0) == AC_REINSTALL);
}

static void test_state_table(void) {
    printf("state table\n");
    CHECK(decide(1, 0, 0, 0, 0, 3, 0) == AC_PROBLEM);
    CHECK(decide(1, 0, 0, 0, 0, 2, 0) == AC_INSTALL);
    CHECK(decide(1, 0, 0, 0, 0, 0, 0) == AC_INSTALL);
    CHECK(decide(1, 0, 1, 1, 1, 0, 0) == AC_NONE);
    CHECK(decide(1, 0, 1, 0, 0, 0, 0) == AC_TURN_OFF);
    CHECK(decide(0, 0, 1, 1, 1, 0, 0) == AC_UNINSTALL);
    CHECK(decide(0, 0, 1, 0, 0, 0, 0) == AC_UNINSTALL);
    CHECK(decide(0, 0, 0, 0, 0, 0, 0) == AC_NONE);
    CHECK(decide(1, 0, 0, 0, 0, 0, 1) == AC_NONE);
    CHECK(decide(0, 0, 1, 1, 1, 0, 1) == AC_NONE);
}

int main(void) {
    snprintf(g_dir, sizeof(g_dir), "/tmp/accopy-XXXXXX");
    if (!mkdtemp(g_dir))
        return 1;
    test_mark();
    test_fail_count();
    test_space();
    test_progress_done();
    test_progress_stall();
    test_progress_timeout();
    test_progress_not_here_yet();
    test_files_done();
    test_state_store();
    test_state_playgo_lost();
    test_state_table();
    test_state_rereg();
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_dir);
    if (system(cmd)) {
    }
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
