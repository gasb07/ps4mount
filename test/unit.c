/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Unit tests for the parts that run without a console: crypto, the install file
 * builders and the param.sfo parser. Build and run with: bash test/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "build.h"
#include "crypto.h"
#include "origin.h"
#include "logfile.h"
#include "pkg.h"
#include "pdb_template.h"

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

static void hex(const char *s, uint8_t *out, size_t n) {
    for (size_t i = 0; i < n; i++)
        sscanf(s + 2 * i, "%2hhx", &out[i]);
}

/* A pkg with only the fields the builders use. */
static void fake_pkg(pkg_t *p, const char *content_id, uint64_t size) {
    memset(p, 0, sizeof(*p));
    snprintf(p->content_id, sizeof(p->content_id), "%s", content_id);
    memcpy(p->title_id, content_id + 7, 9);
    p->pkg_size = size;
    for (int i = 0; i < 32; i++)
        p->digest[i] = (uint8_t)(0xa0 + i);
}

static void test_crypto(void) {
    printf("crypto\n");
    uint8_t h[32], want[32];
    sha256("abc", 3, h);
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", want, 32);
    CHECK(!memcmp(h, want, 32));
    sha256("", 0, h);
    hex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", want, 32);
    CHECK(!memcmp(h, want, 32));
    /* the same digest fed in pieces */
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, "a", 1);
    sha256_update(&c, "bc", 2);
    sha256_final(&c, h);
    hex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", want, 32);
    CHECK(!memcmp(h, want, 32));
    CHECK(crc32c("123456789", 9) == 0xe3069283u);
}

static void test_app_json(void) {
    printf("app.json / ac.json\n");
    pkg_t p;
    fake_pkg(&p, "UP0000-CUSA00001_00-TESTGAME00000000", 1234567);
    buf_t b = {0};
    build_app_json(&b, &p, "/mnt/usb0/ps4/CUSA00001/app.pkg");
    const char *want = "{\"numberOfSplitFiles\":1,\"originalFileSize\":1234567,\"packageDigest\":"
                       "\"A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF\",\"pieces\":[{\"fileOffset\":0,"
                       "\"fileSize\":1234567,\"url\":\"\\/mnt\\/usb0\\/ps4\\/CUSA00001\\/app.pkg\"}]}";
    CHECK(b.len == strlen(want) && !memcmp(b.data, want, b.len));
    buf_free(&b);
}

static void check_pbm(const pkg_t *p, int type) {
    buf_t b = {0};
    build_app_pbm(&b, p, "01.02", type);
    uint64_t blocks = (p->pkg_size + 0xffff) / 0x10000;
    size_t bitmap = (size_t)((blocks + 7) / 8);
    CHECK(b.len == 0x100 + bitmap + 32);
    CHECK(!memcmp(b.data, "pdbm", 4));
    CHECK(!memcmp(b.data + 4, p->title_id, 9));
    CHECK(!memcmp(b.data + 0x0e, "01.02", 5));
    CHECK(le32(b.data + 0x1e) == (uint32_t)type);
    CHECK(le64(b.data + 0x22) == blocks);
    CHECK(!memcmp(b.data + 0x60, p->digest, 32));
    CHECK(le32(b.data + 0xac) == bitmap);
    /* all blocks present; bits past the last block stay clear */
    CHECK(b.data[0x100] == 0xff);
    if (blocks % 8)
        CHECK(b.data[0x100 + bitmap - 1] == (uint8_t)(0xff << (8 - blocks % 8)));
    uint8_t h[32];
    sha256(b.data, b.len - 32, h);
    CHECK(!memcmp(b.data + b.len - 32, h, 32));
    buf_free(&b);
}

static void test_pbm(void) {
    printf("app.pbm / patch.pbm / ac.pbm\n");
    pkg_t p;
    fake_pkg(&p, "UP0000-CUSA00001_00-TESTGAME00000000", 82706432); /* whole blocks */
    check_pbm(&p, PBM_APP);
    check_pbm(&p, PBM_PATCH);
    fake_pkg(&p, "UP0000-CUSA00001_00-TESTADDON0000001", 524288 + 100); /* partial last block */
    check_pbm(&p, PBM_ADDCONT);
    check_pbm(&p, PBM_LICENSE);
}

/* Records of a pdb: u32 version, then (u32 key, u32 len, u32 len, data), CRC32C trailer. */
typedef struct {
    uint32_t key, len;
    const uint8_t *data;
} rec_t;

static int parse_pdb(const uint8_t *d, size_t n, rec_t *out, int max) {
    int c = 0;
    size_t off = 4;
    while (off + 12 <= n - 4 && c < max) {
        out[c].key = le32(d + off);
        out[c].len = le32(d + off + 4);
        out[c].data = d + off + 12;
        off += 12 + out[c].len;
        c++;
    }
    return off == n - 4 ? c : -1;
}

static const rec_t *find(const rec_t *r, int n, uint32_t key) {
    for (int i = 0; i < n; i++)
        if (r[i].key == key)
            return &r[i];
    return NULL;
}

static void test_ac_pdb(void) {
    printf("add-on PlayGo record\n");
    pkg_t p;
    fake_pkg(&p, "UP0000-CUSA00001_00-TESTADDON0000001", 31129600);
    const char *url = "/mnt/usb0/ps4/CUSA00001/addcont/CUSA00001/TESTADDON0000001/ac.pkg";
    buf_t json = {0}, pdb = {0};
    build_app_json(&json, &p, url);
    build_ac_pdb(&pdb, &p, url, &json);
    static rec_t got[256], tmpl[256];
    int n = parse_pdb(pdb.data, pdb.len, got, 256);
    int nt = parse_pdb(k_pdb_addcont_template, sizeof(k_pdb_addcont_template), tmpl, 256);
    CHECK(n > 0 && n == nt);
    for (int i = 0; n > 0 && i < n && i < nt; i++)
        CHECK(got[i].key == tmpl[i].key); /* same records in the same order as the installer */
    CHECK(le32(pdb.data) == le32(k_pdb_addcont_template));
    CHECK(le32(pdb.data + pdb.len - 4) == crc32c(pdb.data, pdb.len - 4));
    const rec_t *r;
    CHECK((r = find(got, n, 0xf4241)) && r->len == 36 && !memcmp(r->data, p.content_id, 36));
    CHECK((r = find(got, n, 0xf42a6)) && r->len == strlen(url) && !memcmp(r->data, url, r->len));
    CHECK((r = find(got, n, 0xf42a7)) && r->len == json.len && !memcmp(r->data, json.data, r->len));
    for (uint32_t k = 0xf4246; k <= 0xf4249; k++)
        CHECK((r = find(got, n, k)) && r->len == 8 && le64(r->data) == p.pkg_size);
    CHECK((r = find(got, n, 0xf431b)) && r->len == 53 && !memcmp(r->data, "0000000000000000-", 17) &&
          !memcmp(r->data + 17, p.content_id, 36));
    buf_free(&json);
    buf_free(&pdb);
}

static void slot(uint8_t *lic, uint8_t v) { memset(lic, v, 0x400); }

static int idx_count(const buf_t *idx) { return (int)le32(idx->data + 4); }

static const uint8_t *idx_label(const buf_t *idx, int i) { return idx->data + 0x20 + i * 0x30; }

static void test_license(void) {
    printf("license files (rif/idx)\n");
    static uint8_t a[0x400], b[0x400], c[0x400];
    slot(a, 0x11);
    slot(b, 0x22);
    slot(c, 0x33);
    const char *game = "UP0000-CUSA00001_00-TESTGAME00000000", *dlc = "UP0000-CUSA00001_00-TESTADDON0000001";

    buf_t r1 = {0}, i1 = {0};
    build_license(&r1, &i1, NULL, 0, NULL, 0, game, a);
    CHECK(r1.len == 0x800 && !memcmp(r1.data, "rifa", 4) && !memcmp(r1.data + 4, game, 19));
    CHECK(!memcmp(r1.data + 0x400, a, 0x400));
    CHECK(i1.len == 0x50 && !memcmp(i1.data, "ridx", 4) && idx_count(&i1) == 1);
    CHECK(!memcmp(i1.data + 9, game, 19) && !memcmp(idx_label(&i1, 0), game + 20, 16));
    CHECK(le64(idx_label(&i1, 0) + 0x10) == 0x400 && le64(idx_label(&i1, 0) + 0x18) == 0x400);

    /* an add-on gets a second slot, the game keeps its own */
    buf_t r2 = {0}, i2 = {0};
    build_license(&r2, &i2, r1.data, r1.len, i1.data, i1.len, dlc, b);
    CHECK(r2.len == 0xc00 && idx_count(&i2) == 2 && i2.len == 0x80);
    CHECK(!memcmp(r2.data + 0x400, a, 0x400) && !memcmp(r2.data + 0x800, b, 0x400));
    CHECK(!memcmp(idx_label(&i2, 1), dlc + 20, 16) && le64(idx_label(&i2, 1) + 0x10) == 0x800);

    /* writing the game again replaces its slot and keeps the add-on */
    buf_t r3 = {0}, i3 = {0};
    build_license(&r3, &i3, r2.data, r2.len, i2.data, i2.len, game, c);
    CHECK(r3.len == 0xc00 && idx_count(&i3) == 2);
    CHECK(!memcmp(r3.data + 0x400, c, 0x400) && !memcmp(r3.data + 0x800, b, 0x400));

    /* files of another content ID prefix, or damaged ones, are replaced */
    buf_t r4 = {0}, i4 = {0};
    build_license(&r4, &i4, r2.data, r2.len, i2.data, i2.len, "UP0000-CUSA00002_00-OTHERGAME0000000", a);
    CHECK(r4.len == 0x800 && idx_count(&i4) == 1);
    buf_t r5 = {0}, i5 = {0};
    build_license(&r5, &i5, r2.data, 0x500, i2.data, i2.len, dlc, b); /* rif shorter than its slots */
    CHECK(r5.len == 0x800 && idx_count(&i5) == 1);

    buf_t *all[] = {&r1, &i1, &r2, &i2, &r3, &i3, &r4, &i4, &r5, &i5};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        buf_free(all[i]);
}

/* A param.sfo with one string and one integer. */
static size_t make_sfo(uint8_t *d) {
    memset(d, 0, 256);
    const char keys[] = "TITLE\0ATTRIBUTE\0";
    uint32_t ko = 20 + 2 * 16, dof = ko + (uint32_t)sizeof(keys);
    memcpy(d, "\0PSF", 4);
    d[4] = 1, d[5] = 1;
    d[8] = (uint8_t)ko, d[12] = (uint8_t)dof, d[16] = 2;
    uint8_t *e = d + 20;
    e[0] = 0, e[2] = 0x04, e[3] = 0x02, e[4] = 6, e[8] = 8, e[12] = 0; /* TITLE: utf8, "Title\0" */
    e += 16;
    e[0] = 6, e[2] = 0x04, e[3] = 0x04, e[4] = 4, e[8] = 4, e[12] = 8; /* ATTRIBUTE: int32 at 8 */
    memcpy(d + ko, keys, sizeof(keys));
    memcpy(d + dof, "Title", 6);
    d[dof + 8] = 0x34, d[dof + 9] = 0x12;
    return dof + 12;
}

static void test_sfo(void) {
    printf("param.sfo\n");
    uint8_t d[256];
    size_t n = make_sfo(d);
    sfo_t s;
    CHECK(sfo_parse(d, n, &s) == 0 && s.count == 2);
    const sfo_item_t *t = sfo_get(&s, "TITLE"), *a = sfo_get(&s, "ATTRIBUTE");
    CHECK(t && !t->is_int && !strcmp(t->sval, "Title"));
    CHECK(a && a->is_int && a->ival == 0x1234);
    CHECK(!sfo_get(&s, "CATEGORY"));
    d[1] = 'X'; /* not an SFO */
    CHECK(sfo_parse(d, n, &s) != 0);

    /* offsets and lengths of a damaged param.sfo must stay inside the buffer */
    n = make_sfo(d);
    d[20 + 12] = 0xf0, d[20 + 13] = 0x0f; /* TITLE value at +0xff0 */
    CHECK(sfo_parse(d, n, &s) != 0);
    n = make_sfo(d);
    d[20 + 4] = 0xff, d[20 + 5] = 0x01; /* TITLE length 511 */
    CHECK(sfo_parse(d, n, &s) != 0);
    n = make_sfo(d);
    d[20 + 16] = 0xf0, d[20 + 17] = 0x0f; /* ATTRIBUTE key at +0xff0 */
    CHECK(sfo_parse(d, n, &s) != 0);
    n = make_sfo(d);
    CHECK(sfo_parse(d, n - 2, &s) != 0); /* ATTRIBUTE int32 cut short */
}

/* A pkg file with one plain entry (id 0x1000) of the given size at 0x1020. */
static int make_pkg(char *path, uint32_t entry_size, const char *data) {
    static uint8_t h[0x1020 + 16];
    memset(h, 0, sizeof(h));
    h[0] = 0x7f, h[1] = 'C', h[2] = 'N', h[3] = 'T';
    h[0x13] = 1;                      /* entry count */
    h[0x1a] = 0x10;                   /* entry table at 0x1000 */
    uint8_t *e = h + 0x1000;
    e[2] = 0x10;                      /* id 0x1000 */
    e[18] = 0x10, e[19] = 0x20;       /* offset 0x1020 */
    e[20] = (uint8_t)(entry_size >> 24), e[21] = (uint8_t)(entry_size >> 16);
    e[22] = (uint8_t)(entry_size >> 8), e[23] = (uint8_t)entry_size;
    memcpy(h + 0x1020, data, strlen(data));
    strcpy(path, "/tmp/ps4mount-unit-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0)
        return -1;
    int rc = write(fd, h, sizeof(h)) == (ssize_t)sizeof(h) ? 0 : -1;
    close(fd);
    return rc;
}

static void test_pkg_entry(void) {
    printf("pkg entries\n");
    char path[64];
    pkg_t *p = malloc(sizeof(*p));
    size_t len = 0;
    CHECK(p && !make_pkg(path, 5, "hello") && !pkg_open(p, path));
    uint8_t *d = pkg_entry(p, 0x1000, &len);
    CHECK(d && len == 5 && !memcmp(d, "hello", 5));
    free(d);
    pkg_close(p);
    unlink(path);

    /* a size near 4 GB used to wrap the padded size to 0 and write far out of bounds */
    CHECK(!make_pkg(path, 0xfffffff8u, "hello") && !pkg_open(p, path));
    CHECK(!pkg_entry(p, 0x1000, &len));
    pkg_close(p);
    unlink(path);
    free(p);
}

static void touch(const char *path) {
    FILE *f = fopen(path, "w");
    if (f)
        fclose(f);
}

static void test_origin(void) {
    printf("origin records and restored list\n");
    char root[] = "/tmp/ps4mount-test-XXXXXX", game[128], list[128], out[256], p[256];
    if (!mkdtemp(root)) {
        CHECK(0);
        return;
    }
    snprintf(game, sizeof(game), "%s/ps4", root);
    mkdir(game, 0777);
    snprintf(game, sizeof(game), "%s/ps4/CUSA00001", root);
    mkdir(game, 0777);

    CHECK(origin_find(game, "app.pkg", out, sizeof(out)) != 0); /* nothing recorded yet */
    CHECK(origin_add(game, "app.pkg", "games/My Game [v1.00].pkg") == 0);
    CHECK(origin_add(game, "patch.pkg", "Update 1.01.pkg") == 0);
    CHECK(origin_add(game, "addcont/CUSA00001/TESTADDON0000001/ac.pkg", "dlc/Pack.pkg") == 0);
    CHECK(origin_find(game, "app.pkg", out, sizeof(out)) == 0 && !strcmp(out, "games/My Game [v1.00].pkg"));
    CHECK(origin_find(game, "addcont/CUSA00001/TESTADDON0000001/ac.pkg", out, sizeof(out)) == 0 &&
          !strcmp(out, "dlc/Pack.pkg"));
    /* a newer update in the same place: the last record wins, the old one is kept aside */
    CHECK(origin_add(game, "replaced/patch-1.pkg", "Update 1.01.pkg") == 0);
    CHECK(origin_add(game, "patch.pkg", "Update 1.02.pkg") == 0);
    CHECK(origin_find(game, "patch.pkg", out, sizeof(out)) == 0 && !strcmp(out, "Update 1.02.pkg"));
    CHECK(origin_find(game, "replaced/patch-1.pkg", out, sizeof(out)) == 0 && !strcmp(out, "Update 1.01.pkg"));
    CHECK(origin_find(game, "patch", out, sizeof(out)) != 0); /* whole names only */
    CHECK(origin_add(game, "bad\tname", "x.pkg") != 0);

    /* free names in the original folder */
    snprintf(p, sizeof(p), "%s/Game.pkg", root);
    CHECK(origin_free_path(root, "Game.pkg", out, sizeof(out)) == 0 && !strcmp(out, p));
    touch(p);
    snprintf(p, sizeof(p), "%s/Game (1).pkg", root);
    CHECK(origin_free_path(root, "Game.pkg", out, sizeof(out)) == 0 && !strcmp(out, p));
    touch(p);
    snprintf(p, sizeof(p), "%s/Game (2).pkg", root);
    CHECK(origin_free_path(root, "Game.pkg", out, sizeof(out)) == 0 && !strcmp(out, p));
    snprintf(p, sizeof(p), "%s/v1.0/noext", root);
    CHECK(origin_free_path(root, "v1.0/noext", out, sizeof(out)) == 0 && !strcmp(out, p));

    /* restored pkgs are matched by path and size */
    snprintf(list, sizeof(list), "%s/restored.txt", root);
    CHECK(!restored_has(list, "Game.pkg", 100));
    CHECK(restored_add(list, "Game.pkg", 100) == 0);
    CHECK(restored_add(list, "games/Other.pkg", 5000000000ull) == 0);
    CHECK(restored_has(list, "Game.pkg", 100));
    CHECK(restored_has(list, "games/Other.pkg", 5000000000ull));
    CHECK(!restored_has(list, "Game.pkg", 101));    /* changed */
    CHECK(!restored_has(list, "Game2.pkg", 100));   /* renamed */
    CHECK(!restored_has(list, "x/Game.pkg", 100));  /* moved */

    char cmd[300];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

static size_t file_text(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(out, 1, cap - 1, f) : 0;
    out[n] = 0;
    if (f)
        fclose(f);
    return n;
}

static void test_log_trim(void) {
    printf("log trim\n");
    char path[] = "/tmp/ps4mount-log-XXXXXX", text[256];
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    const char log[] = "line one\nline two\nline three\nline four\n";
    CHECK(write(fd, log, sizeof(log) - 1) == (ssize_t)(sizeof(log) - 1));
    close(fd);

    CHECK(log_trim(path, 1000, 20) == 0); /* under the limit: untouched */
    CHECK(file_text(path, text, sizeof(text)) == sizeof(log) - 1);

    /* over the limit: the end is kept, starting at a whole line */
    CHECK(log_trim(path, 30, 20) == 1);
    file_text(path, text, sizeof(text));
    CHECK(!strcmp(text, "line four\n"));
    unlink(path);

    CHECK(log_trim("/tmp/ps4mount-no-such-log", 30, 20) == 0);
}

static void test_playgo_bounds(void) {
    printf("playgo: table offsets that wrap past 4 GB are refused\n");
    uint8_t pg[0x100] = {0};
    memcpy(pg, "plgo", 4);
    pg[0x0a] = 1;
    pg[0x0e] = 1;
    /* scenario table (index 4): offset 0xFFFFFF00 + size 0x200 wraps to 0x100 in 32 bits */
    uint8_t *e = pg + 0xc0 + 4 * 8;
    e[0] = 0x00, e[1] = 0xff, e[2] = 0xff, e[3] = 0xff;
    e[4] = 0x00, e[5] = 0x02;
    buf_t b = {0};
    CHECK(build_app_xml(&b, pg, sizeof(pg)) == -1);
    buf_free(&b);
}

int main(void) {
    test_crypto();
    test_app_json();
    test_pbm();
    test_ac_pdb();
    test_license();
    test_sfo();
    test_origin();
    test_pkg_entry();
    test_log_trim();
    test_playgo_bounds();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
