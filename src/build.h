/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Builders for install files. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pkg.h"

/* failed is set when memory ran out: the buffer stops growing and its content is incomplete,
 * so it must not be written to a file. */
typedef struct {
    uint8_t *data;
    size_t len, cap;
    int failed;
} buf_t;

void buf_put(buf_t *b, const void *data, size_t len);
void buf_printf(buf_t *b, const char *fmt, ...);
void buf_free(buf_t *b);

/* /user/app/<TID>/app.json; url is the pkg path as the console sees it. */
void build_app_json(buf_t *out, const pkg_t *pkg, const char *url);
/* app.pbm, patch.pbm and ac.pbm (+ .backup); type is one of PBM_*. */
#define PBM_APP 0x11
#define PBM_PATCH 0x31
#define PBM_ADDCONT 0x12 /* add-on with data */
#define PBM_LICENSE 0x00 /* add-on that only unlocks content */
void build_app_pbm(buf_t *out, const pkg_t *pkg, const char *app_ver, int type);
/* /user/app/<TID>/app.xml from playgo-chunk.dat. */
int build_app_xml(buf_t *out, const uint8_t *playgo, size_t len);
/* Update side of a title, all NULL/0 when there is no patch. */
typedef struct {
    const pkg_t *pkg;
    const uint8_t *playgo;
    size_t playgo_len;
    const char *ver, *url;
    const buf_t *json;
} patch_ref_t;

/* /system_data/playgo/<TID>/pfs.pbm */
int build_pfs_pbm(buf_t *out, const pkg_t *pkg, const uint8_t *playgo, size_t len, const patch_ref_t *patch);
/* /user/playgo/app/<CID>/00/d0.pdb (and d1.pdb); ver is the effective (patch) version. */
int build_pdb(buf_t *out, const pkg_t *pkg, const char *ver, const char *url, const buf_t *app_json,
              const uint8_t *playgo, size_t len, const patch_ref_t *patch);
/* /user/playgo/patch/<CID>/00/00.00/d0.pdb (and d1.pdb). */
int build_patch_pdb(buf_t *out, const patch_ref_t *patch);
/* /user/playgo/addcont/<CID>/00/d0.pdb (and d1.pdb). */
void build_ac_pdb(buf_t *out, const pkg_t *pkg, const char *url, const buf_t *ac_json);
/* /user/license/fake<CID[:19]>.rif and .idx: the current files (NULL when missing)
 * with the slot of content_id added or replaced. */
void build_license(buf_t *rif, buf_t *idx, const uint8_t *old_rif, size_t rif_len, const uint8_t *old_idx,
                   size_t idx_len, const char *content_id, const uint8_t *license_dat);

uint16_t playgo_chunk_count(const uint8_t *playgo, size_t len);
