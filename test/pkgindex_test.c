/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Tests for the pkg index, on real folders. A test pkg is a text file:
 * "<gd|gp|ac|xx> <title ID> <version> [short]". Build and run with: bash test/run.sh */
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "pkgindex.h"

static int g_failed, g_checks, g_reads;
static char g_root[PATH_MAX];

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

static void read_test(pidx_pkg_t *p) {
    g_reads++;
    FILE *f = fopen(p->path, "r");
    char kind[8] = "", tid[16] = "", ver[16] = "", extra[16] = "";
    if (!f)
        return;
    int n = fscanf(f, "%7s %15s %15s %15s", kind, tid, ver, extra);
    fclose(f);
    if (n < 3)
        return;
    p->kind = !strcmp(kind, "gd") ? PIDX_GAME : !strcmp(kind, "gp") ? PIDX_UPDATE : !strcmp(kind, "ac") ? PIDX_ADDON : PIDX_OTHER;
    snprintf(p->title_id, sizeof(p->title_id), "%.9s", tid);
    snprintf(p->ver, sizeof(p->ver), "%.7s", ver);
    p->incomplete = !strcmp(extra, "short");
}

static void mkdirs(const char *path) {
    char p[PATH_MAX];
    snprintf(p, sizeof(p), "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            mkdir(p, 0777);
            *s = '/';
        }
    mkdir(p, 0777);
}

static void put(const char *rel, const char *text) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof(p), "%s/%s", g_root, rel);
    char *slash = strrchr(p, '/');
    *slash = 0;
    mkdirs(p);
    *slash = '/';
    FILE *f = fopen(p, "w");
    fputs(text, f);
    fclose(f);
}

static void fresh_root(const char *name) {
    char cmd[PATH_MAX + 16];
    snprintf(g_root, sizeof(g_root), "build/test/pkgindex/%s", name);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_root);
    if (system(cmd))
        exit(1);
    mkdirs(g_root);
}

static const pidx_pkg_t *find(const pidx_t *idx, const char *rel) {
    char p[PATH_MAX + 64];
    snprintf(p, sizeof(p), "%s/%s", g_root, rel);
    for (int i = 0; i < idx->count; i++)
        if (!strcmp(idx->pkgs[i].path, p))
            return &idx->pkgs[i];
    return NULL;
}

static const char k_root_only[1][128] = {""};

static void test_walk(void) {
    fresh_root("walk");
    put("a.pkg", "gd CUSA00001 01.00");
    put("games/deep/b.PKG", "gd CUSA00002 01.00");
    put("games/notes.txt", "gd CUSA00003 01.00");
    put(".hidden/c.pkg", "gd CUSA00004 01.00");
    put("$RECYCLE.BIN/d.pkg", "gd CUSA00005 01.00");
    put("System Volume Information/e.pkg", "gd CUSA00006 01.00");
    put("1/2/3/4/5/6/7/8/f.pkg", "gd CUSA00007 01.00");
    put("1/2/3/4/5/6/7/8/9/g.pkg", "gd CUSA00008 01.00");
    char link[PATH_MAX + 16], target[PATH_MAX];
    mkdirs("build/test/pkgindex/outside");
    FILE *f = fopen("build/test/pkgindex/outside/h.pkg", "w");
    fputs("gd CUSA00009 01.00", f);
    fclose(f);
    snprintf(link, sizeof(link), "%s/link", g_root);
    CHECK(realpath("build/test/pkgindex/outside", target) != NULL);
    CHECK(!symlink(target, link));
    pidx_t idx = {0};
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 3);
    CHECK(find(&idx, "a.pkg"));
    CHECK(find(&idx, "games/deep/b.PKG"));
    CHECK(find(&idx, "1/2/3/4/5/6/7/8/f.pkg"));
    CHECK(!find(&idx, "1/2/3/4/5/6/7/8/9/g.pkg"));
    CHECK(!find(&idx, "link/h.pkg"));
    for (int i = 1; i < idx.count; i++)
        CHECK(strcmp(idx.pkgs[i - 1].path, idx.pkgs[i].path) < 0);
    pidx_free(&idx);
    CHECK(!idx.pkgs && !idx.count);
}

static void test_folders(void) {
    fresh_root("folders");
    put("root.pkg", "gd CUSA00001 01.00");
    put("games/a.pkg", "gd CUSA00002 01.00");
    put("games/sub/b.pkg", "gd CUSA00003 01.00");
    put("other/c.pkg", "gd CUSA00004 01.00");
    put("ps4/CUSA00005/app.pkg", "gd CUSA00005 01.00");
    const char folders[3][128] = {"games", "games/sub", "missing"};
    pidx_t idx = {0};
    g_reads = 0;
    CHECK(pidx_scan(&idx, g_root, folders, 3, read_test) == 3); /* a folder inside another counts once */
    CHECK(g_reads == 3);
    CHECK(find(&idx, "games/a.pkg") && find(&idx, "games/sub/b.pkg"));
    CHECK(find(&idx, "ps4/CUSA00005/app.pkg")); /* always searched */
    CHECK(!find(&idx, "root.pkg") && !find(&idx, "other/c.pkg"));
    pidx_free(&idx);
    /* the root as a folder takes in ps4 too: still once */
    pidx_t all = {0};
    g_reads = 0;
    CHECK(pidx_scan(&all, g_root, k_root_only, 1, read_test) == 5);
    CHECK(g_reads == 5);
    pidx_free(&all);
}

static void test_cache(void) {
    fresh_root("cache");
    put("a.pkg", "gd CUSA00001 01.00");
    put("b.pkg", "gd CUSA00002 01.00");
    put("c.pkg", "xx");
    pidx_t idx = {0};
    g_reads = 0;
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 3);
    CHECK(g_reads == 3);
    g_reads = 0;
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 3);
    CHECK(g_reads == 0); /* an unusable pkg is not read again either */
    put("a.pkg", "gd CUSA00001 01.05 more");
    g_reads = 0;
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 3);
    CHECK(g_reads == 1);
    CHECK(!strcmp(find(&idx, "a.pkg")->ver, "01.05"));
    /* same size, other time */
    char p[PATH_MAX + 16];
    snprintf(p, sizeof(p), "%s/b.pkg", g_root);
    struct stat st;
    CHECK(!stat(p, &st));
    struct timespec ts[2] = {{st.st_mtime + 100, 0}, {st.st_mtime + 100, 0}};
    CHECK(!utimensat(AT_FDCWD, p, ts, 0));
    g_reads = 0;
    pidx_scan(&idx, g_root, k_root_only, 1, read_test);
    CHECK(g_reads == 1);
    /* a pkg that is gone leaves the index */
    CHECK(!unlink(p));
    g_reads = 0;
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 2);
    CHECK(g_reads == 0 && !find(&idx, "b.pkg"));
    /* one that came back while the index was empty */
    put("d.pkg", "gd CUSA00004 01.00");
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, read_test) == 3);
    pidx_free(&idx);
}

static void test_group(void) {
    fresh_root("group");
    put("z/old.pkg", "gd CUSA00001 01.00");
    put("a/new.pkg", "gd CUSA00001 01.05");
    put("m/same.pkg", "gd CUSA00001 01.05");
    put("u1.pkg", "gp CUSA00001 01.02");
    put("u2.pkg", "gp CUSA00001 01.10");
    put("u3.pkg", "gp CUSA00001 01.03");
    put("x1.pkg", "ac CUSA00001 01.00");
    put("x2.pkg", "ac CUSA00001 01.00");
    put("half.pkg", "gd CUSA00002 01.00 short");
    put("halfu.pkg", "gp CUSA00001 09.00 short");
    put("g3.pkg", "gd CUSA00003 01.00");
    put("lone.pkg", "gp CUSA00004 01.01");
    put("bad.pkg", "xx");
    pidx_t idx = {0};
    pidx_scan(&idx, g_root, k_root_only, 1, read_test);
    pidx_game_t g[8];
    int n = pidx_group(&idx, g, 8);
    CHECK(n == 3);
    CHECK(!strcmp(g[0].title_id, "CUSA00001") && !strcmp(g[1].title_id, "CUSA00003") && !strcmp(g[2].title_id, "CUSA00004"));
    CHECK(g[0].game == find(&idx, "a/new.pkg") - idx.pkgs);
    CHECK(g[0].copies == 2);
    CHECK(g[0].update == find(&idx, "u2.pkg") - idx.pkgs);
    CHECK(g[0].addons == 2);
    CHECK(g[1].game >= 0 && g[1].update == -1 && !g[1].addons && !g[1].copies);
    CHECK(g[2].game == -1 && g[2].update >= 0); /* an update waiting for its game */
    CHECK(pidx_group(&idx, g, 2) == 2);
    pidx_free(&idx);
}

static void test_real_reader(void) {
    fresh_root("real");
    put("junk.pkg", "not a pkg");
    pidx_t idx = {0};
    CHECK(pidx_scan(&idx, g_root, k_root_only, 1, pidx_read_pkg) == 1);
    CHECK(idx.pkgs[0].kind == PIDX_OTHER && !idx.pkgs[0].title_id[0]);
    pidx_free(&idx);
}

int main(void) {
    mkdirs("build/test/pkgindex");
    test_walk();
    test_folders();
    test_cache();
    test_group();
    test_real_reader();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
