/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * PS4 pkg reader. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define PKG_MAX_ENTRIES 512
#define PKG_MAX_ENTRY_SIZE (64u << 20) /* largest entry pkg_entry reads */

typedef struct {
    uint32_t id, flags1, flags2, offset, size;
    uint8_t raw[32];
} pkg_entry_t;

typedef struct {
    int fd;
    uint8_t header[0x1000];
    char content_id[37];
    char title_id[10];
    uint64_t pkg_size, pfs_offset, pfs_size;
    uint64_t file_size; /* entries must lie inside the file */
    uint8_t digest[32];
    int nentries;
    pkg_entry_t entries[PKG_MAX_ENTRIES];
    uint8_t dk3[32];
    int have_dk3, dk3_tried;
} pkg_t;

/* Opens and parses a PS4 pkg. Returns 0 on success. */
int pkg_open(pkg_t *pkg, const char *path);
void pkg_close(pkg_t *pkg);
/* 1 when the pkg uses the public fake key (unwraps it on first use). */
int pkg_fake_key(pkg_t *pkg);

/* Reads (and decrypts when needed) an entry. Caller frees. NULL when missing. */
uint8_t *pkg_entry(pkg_t *pkg, uint32_t id, size_t *len);

typedef struct {
    char key[64];
    int is_int;
    uint32_t ival;
    char sval[512];
} sfo_item_t;

typedef struct {
    int count;
    sfo_item_t items[128];
} sfo_t;

int sfo_parse(const uint8_t *data, size_t len, sfo_t *out);
const sfo_item_t *sfo_get(const sfo_t *sfo, const char *key);
