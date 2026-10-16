/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Builders for the files a PS4 pkg install leaves behind. */
/* Builders for the files a PS4 pkg install leaves behind. Formats were taken
 * from real installs on FW 13.20 and are checked against them in test/. */
#include "build.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "pdb_template.h"

#define BLK 0x10000ull

void buf_put(buf_t *b, const void *data, size_t len) {
    if (b->failed)
        return;
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1)
            cap *= 2;
        uint8_t *grown = realloc(b->data, cap);
        if (!grown) {
            b->failed = 1;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
}

void buf_printf(buf_t *b, const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        buf_put(b, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

void buf_free(buf_t *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put_le64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

static void json_escape(buf_t *b, const char *s) {
    for (; *s; s++) {
        if (*s == '/' || *s == '"' || *s == '\\')
            buf_put(b, "\\", 1);
        buf_put(b, s, 1);
    }
}

void build_app_json(buf_t *out, const pkg_t *pkg, const char *url) {
    buf_printf(out, "{\"numberOfSplitFiles\":1,\"originalFileSize\":%llu,\"packageDigest\":\"",
               (unsigned long long)pkg->pkg_size);
    for (int i = 0; i < 32; i++)
        buf_printf(out, "%02X", pkg->digest[i]);
    buf_printf(out, "\",\"pieces\":[{\"fileOffset\":0,\"fileSize\":%llu,\"url\":\"", (unsigned long long)pkg->pkg_size);
    json_escape(out, url);
    buf_printf(out, "\"}]}");
}

static void put_bitmap(buf_t *out, uint64_t nbits) {
    size_t nbytes = (size_t)((nbits + 7) / 8);
    uint8_t *bm = malloc(nbytes);
    if (!bm) {
        out->failed = 1;
        return;
    }
    memset(bm, 0xff, nbytes);
    if (nbits % 8)
        bm[nbytes - 1] = (uint8_t)(0xff << (8 - nbits % 8));
    buf_put(out, bm, nbytes);
    free(bm);
}

static void put_sha_trailer(buf_t *out) {
    uint8_t h[32];
    sha256(out->data, out->len, h);
    buf_put(out, h, 32);
}

void build_app_pbm(buf_t *out, const pkg_t *pkg, const char *app_ver, int type) {
    uint64_t nblk = (pkg->pkg_size + BLK - 1) / BLK;
    uint8_t hdr[0x100] = {0};
    memcpy(hdr, "pdbm", 4);
    memcpy(hdr + 4, pkg->title_id, 9);
    memcpy(hdr + 0x0e, app_ver, strnlen(app_ver, 5));
    put_le32(hdr + 0x14, 0x990);
    put_le32(hdr + 0x18, 0x10000);
    put_le32(hdr + 0x1e, (uint32_t)type);
    put_le64(hdr + 0x22, nblk);
    memcpy(hdr + 0x60, pkg->digest, 32);
    put_le32(hdr + 0xa0, 1);
    put_le32(hdr + 0xa8, 0x100);
    put_le32(hdr + 0xac, (uint32_t)((nblk + 7) / 8));
    buf_put(out, hdr, sizeof(hdr));
    put_bitmap(out, nblk);
    put_sha_trailer(out);
}

/* playgo-chunk.dat: header counts, then (offset,size) pairs at 0xC0 for
 * chunk attrs, chunk mchunks, labels, mchunk attrs, scenario attrs,
 * scenario chunks, scenario labels, inner mchunk attrs. */
static int playgo_table(const uint8_t *pg, size_t len, int idx, uint32_t *off, uint32_t *size) {
    if (len < 0x100 || memcmp(pg, "plgo", 4))
        return -1;
    *off = le32(pg + 0xc0 + idx * 8);
    *size = le32(pg + 0xc4 + idx * 8);
    return (uint64_t)*off + *size <= len ? 0 : -1;
}

uint16_t playgo_chunk_count(const uint8_t *pg, size_t len) {
    return pg && len >= 0x10 ? le16(pg + 0x0a) : 1;
}

int build_app_xml(buf_t *out, const uint8_t *pg, size_t len) {
    uint16_t chunks = playgo_chunk_count(pg, len), scenarios = pg && len >= 0x10 ? le16(pg + 0x0e) : 1;
    uint32_t so = 0, ss = 0, co = 0, cs = 0;
    if (!pg || playgo_table(pg, len, 4, &so, &ss) || playgo_table(pg, len, 5, &co, &cs))
        return -1;
    buf_printf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<psproject fmt=\"playgo-status\" version=\"1000\">"
                    "<chunk_status chunk_count=\"%u\"><chunks language_mask=\"18446744073709551615\">", chunks);
    for (unsigned i = 0; i < chunks; i++)
        buf_printf(out, "<chunk id=\"%u\" locus=\"3\" req_locus=\"3\"/>", i);
    buf_printf(out, "</chunks><scenarios>");
    for (unsigned s = 0; s < scenarios && (s + 1) * 32 <= ss; s++) {
        const uint8_t *a = pg + so + s * 32;
        uint32_t first = le32(a + 0x10) / 2;
        uint16_t count = le16(a + 0x16);
        buf_printf(out, "<scenario>");
        for (unsigned i = 0; i < count && (first + i) * 2 + 2 <= cs; i++)
            buf_printf(out, i ? " %u" : "%u", le16(pg + co + (first + i) * 2));
        buf_printf(out, "</scenario>");
    }
    buf_printf(out, "</scenarios></chunk_status></psproject>");
    return 0;
}

static uint64_t playgo_inner_size(const uint8_t *pg, size_t len) {
    uint32_t io, isz;
    if (playgo_table(pg, len, 7, &io, &isz))
        return 0;
    uint64_t total = 0;
    for (uint32_t i = 0; i + 16 <= isz; i += 16)
        total += le64(pg + io + i + 8);
    return total;
}

static size_t align16(size_t x) { return (x + 15) & ~(size_t)15; }

int build_pfs_pbm(buf_t *out, const pkg_t *pkg, const uint8_t *pg, size_t len, const patch_ref_t *patch) {
    const pkg_t *pk[2] = {pkg, patch ? patch->pkg : NULL};
    uint64_t bits[4] = {0};
    uint8_t hdr[0x100] = {0};
    static const uint8_t fixed[14] = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x02, 0x00, 0x00, 0x00};
    memcpy(hdr, "ppbm", 4);
    memcpy(hdr + 4, pkg->title_id, 9);
    if (patch)
        memcpy(hdr + 0x0e, patch->ver, strnlen(patch->ver, 5));
    memcpy(hdr + 0x14, fixed, sizeof(fixed));
    for (int i = 0; i < 2 && pk[i]; i++) {
        uint64_t inner = i ? playgo_inner_size(patch->playgo, patch->playgo_len) : playgo_inner_size(pg, len);
        if (!inner)
            return -1;
        bits[i * 2] = (pk[i]->pfs_size + BLK - 1) / BLK;
        bits[i * 2 + 1] = (inner + BLK - 1) / BLK;
        uint8_t *h = hdr + 0x22 + i * 0x20;
        put_le64(h, (pk[i]->pkg_size + BLK - 1) / BLK);
        put_le64(h + 8, pk[i]->pfs_offset / BLK);
        put_le64(h + 16, bits[i * 2]);
        put_le64(h + 24, bits[i * 2 + 1]);
    }
    /* the last patch count is 48-bit: the app digest starts right after it */
    for (int i = 0; i < 2 && pk[i]; i++)
        memcpy(hdr + 0x60 + i * 0x20, pk[i]->digest, 32);
    int sections = patch ? 4 : 2;
    size_t off = 0x100;
    for (int i = 0; i < sections; i++) {
        size_t n = (size_t)((bits[i] + 7) / 8);
        put_le64(hdr + 0xc0 + i * 16, 1);
        put_le32(hdr + 0xc8 + i * 16, (uint32_t)off);
        put_le32(hdr + 0xcc + i * 16, (uint32_t)n);
        off = align16(off + n);
    }
    buf_put(out, hdr, sizeof(hdr));
    static const uint8_t zero[16] = {0};
    for (int i = 0; i < sections; i++) {
        put_bitmap(out, bits[i]);
        buf_put(out, zero, align16(out->len) - out->len);
    }
    put_sha_trailer(out);
    return 0;
}

static void pdb_record(buf_t *b, uint32_t key, const void *data, uint32_t len) {
    uint8_t h[12];
    put_le32(h, key);
    put_le32(h + 4, len);
    put_le32(h + 8, len);
    buf_put(b, h, 12);
    buf_put(b, data, len);
}

static void ver_int(uint8_t out[4], const char *ver) {
    unsigned major = (unsigned)strtoul(ver, NULL, 16), minor = (unsigned)strtoul(ver + 3, NULL, 16);
    put_le32(out, major << 8 | minor);
}

typedef struct {
    uint32_t key;
    const void *data;
    uint32_t len;
} pdb_override_t;

/* Re-emits a template record list with overrides and a CRC32C trailer. */
static void pdb_from_template(buf_t *out, const uint8_t *t, size_t tlen, const pdb_override_t *ov, int nov) {
    buf_put(out, t, 4);
    size_t off = 4;
    while (off + 12 <= tlen - 4) {
        uint32_t key = le32(t + off), l = le32(t + off + 4);
        const void *data = t + off + 12;
        uint32_t dl = l;
        for (int i = 0; i < nov; i++)
            if (ov[i].key == key) {
                data = ov[i].data;
                dl = ov[i].len;
            }
        pdb_record(out, key, data, dl);
        off += 12 + l;
    }
    uint8_t crc[4];
    put_le32(crc, crc32c(out->data, out->len));
    buf_put(out, crc, 4);
}

static void chunk_lists(const uint8_t *pg, size_t len, uint8_t **loci, uint8_t **list, uint16_t *chunks) {
    *chunks = playgo_chunk_count(pg, len);
    *loci = malloc(*chunks);
    *list = malloc(*chunks * 4u);
    for (unsigned i = 0; i < *chunks; i++) {
        (*loci)[i] = 3;
        put_le16(*list + i * 4, (uint16_t)i);
        put_le16(*list + i * 4 + 2, 3);
    }
}

int build_pdb(buf_t *out, const pkg_t *pkg, const char *ver, const char *url, const buf_t *app_json,
              const uint8_t *pg, size_t len, const patch_ref_t *patch) {
    uint8_t *loci, *list, size[8], vi[4];
    uint16_t chunks;
    chunk_lists(pg, len, &loci, &list, &chunks);
    put_le64(size, pkg->pkg_size + (patch ? patch->pkg->pkg_size : 0));
    ver_int(vi, ver);
    pdb_override_t ov[] = {
        {0xf4241, pkg->content_id, 36},
        {0xf424d, "", 0}, {0xf4260, "", 0},
        {0xf430b, loci, chunks}, {0xf430c, list, chunks * 4u},
        {0xf42a6, url, (uint32_t)strlen(url)}, {0xf42a7, app_json->data, (uint32_t)app_json->len},
        {0xf42a9, patch ? patch->url : "", patch ? (uint32_t)strlen(patch->url) : 0},
        {0xf42aa, patch ? (const void *)patch->json->data : "", patch ? (uint32_t)patch->json->len : 0},
        {0xf42ab, ver, (uint32_t)strlen(ver)}, {0xf42bb, vi, 4},
        {0xf4246, size, 8}, {0xf4247, size, 8}, {0xf4248, size, 8}, {0xf4249, size, 8},
    };
    pdb_from_template(out, k_pdb_template, sizeof(k_pdb_template), ov, (int)(sizeof(ov) / sizeof(ov[0])));
    free(loci);
    free(list);
    return 0;
}

int build_patch_pdb(buf_t *out, const patch_ref_t *patch) {
    uint8_t *loci, *list, size[8];
    uint16_t chunks;
    chunk_lists(patch->playgo, patch->playgo_len, &loci, &list, &chunks);
    put_le64(size, patch->pkg->pkg_size);
    char base_ref[64];
    snprintf(base_ref, sizeof(base_ref), "0000000000000000-%.36s", patch->pkg->content_id);
    pdb_override_t ov[] = {
        {0xf4241, patch->pkg->content_id, 36},
        {0xf424d, "", 0},
        {0xf430b, loci, chunks},
        {0xf42a6, patch->url, (uint32_t)strlen(patch->url)},
        {0xf42a7, patch->json->data, (uint32_t)patch->json->len},
        {0xf42ab, patch->ver, (uint32_t)strlen(patch->ver)}, {0xf42ba, patch->ver, (uint32_t)strlen(patch->ver)},
        {0xf4246, size, 8}, {0xf4247, size, 8}, {0xf4248, size, 8}, {0xf4249, size, 8},
        {0xf431b, base_ref, (uint32_t)strlen(base_ref)},
    };
    pdb_from_template(out, k_pdb_patch_template, sizeof(k_pdb_patch_template), ov, (int)(sizeof(ov) / sizeof(ov[0])));
    free(loci);
    free(list);
    return 0;
}

void build_ac_pdb(buf_t *out, const pkg_t *pkg, const char *url, const buf_t *ac_json) {
    uint8_t size[8];
    put_le64(size, pkg->pkg_size);
    char ref[64];
    snprintf(ref, sizeof(ref), "0000000000000000-%.36s", pkg->content_id);
    pdb_override_t ov[] = {
        {0xf4241, pkg->content_id, 36},
        {0xf42a6, url, (uint32_t)strlen(url)}, {0xf42a7, ac_json->data, (uint32_t)ac_json->len},
        {0xf4246, size, 8}, {0xf4247, size, 8}, {0xf4248, size, 8}, {0xf4249, size, 8},
        {0xf431b, ref, (uint32_t)strlen(ref)},
    };
    pdb_from_template(out, k_pdb_addcont_template, sizeof(k_pdb_addcont_template), ov, (int)(sizeof(ov) / sizeof(ov[0])));
}

/* One rif/idx pair per content id prefix (UP9000-CUSA00000_00): a 0x400 header,
 * then one 0x400 license slot per content (the game and its add-ons), each
 * listed in the idx by the label after the prefix. */
#define RIF_SLOT 0x400
#define IDX_HEAD 0x20
#define IDX_ENTRY 0x30

void build_license(buf_t *rif, buf_t *idx, const uint8_t *old_rif, size_t rif_len, const uint8_t *old_idx,
                   size_t idx_len, const char *content_id, const uint8_t *license_dat) {
    const char *label = content_id + 20;
    uint32_t count = 0, slot = 0;
    int valid = old_rif && old_idx && rif_len >= RIF_SLOT && idx_len >= IDX_HEAD && !memcmp(old_rif, "rifa", 4) &&
                !memcmp(old_idx, "ridx", 4) && !memcmp(old_rif + 4, content_id, 19);
    if (valid) {
        count = le32(old_idx + 4);
        if (idx_len < IDX_HEAD + (size_t)count * IDX_ENTRY || rif_len < RIF_SLOT * (size_t)(count + 1))
            valid = 0;
    }
    if (!valid)
        count = 0;
    for (slot = 0; slot < count; slot++)
        if (!memcmp(old_idx + IDX_HEAD + slot * IDX_ENTRY, label, 16))
            break;
    uint32_t total = slot == count ? count + 1 : count;

    uint8_t head[RIF_SLOT] = {0};
    memcpy(head, "rifa", 4);
    memcpy(head + 4, content_id, 19);
    buf_put(rif, head, sizeof(head));
    for (uint32_t i = 0; i < total; i++)
        buf_put(rif, i == slot ? license_dat : old_rif + RIF_SLOT * (i + 1), RIF_SLOT);

    uint8_t x[IDX_HEAD] = {0};
    memcpy(x, "ridx", 4);
    put_le32(x + 4, total);
    x[8] = 1;
    memcpy(x + 9, content_id, 19);
    buf_put(idx, x, sizeof(x));
    for (uint32_t i = 0; i < total; i++) {
        uint8_t e[IDX_ENTRY] = {0};
        memcpy(e, i == slot ? (const uint8_t *)label : old_idx + IDX_HEAD + i * IDX_ENTRY, 16);
        put_le64(e + 0x10, (uint64_t)RIF_SLOT * (i + 1));
        put_le64(e + 0x18, RIF_SLOT);
        e[0x20] = 1;
        buf_put(idx, e, sizeof(e));
    }
}
