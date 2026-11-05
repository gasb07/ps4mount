/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Index of the PS4 pkgs of a drive: walks the folders, reads each pkg once and groups them
 * per title. Nothing on the drive is moved, renamed or written. */
#include "pkgindex.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "pkg.h"

/* Folders no pkg of the player is in: hidden ones and what Windows keeps on a drive. */
static int skip_folder(const char *name) {
    return name[0] == '.' || !strcasecmp(name, "$RECYCLE.BIN") || !strcasecmp(name, "System Volume Information");
}

static int is_pkg_name(const char *name) {
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".pkg");
}

static int path_cmp(const void *a, const void *b) {
    return strcmp(((const pidx_pkg_t *)a)->path, ((const pidx_pkg_t *)b)->path);
}

/* The pkgs from the last scan are sorted, the ones found in this scan follow them. */
static pidx_pkg_t *find_pkg(pidx_t *idx, int sorted, const char *path) {
    int lo = 0, hi = sorted - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2, c = strcmp(path, idx->pkgs[mid].path);
        if (!c)
            return &idx->pkgs[mid];
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    for (int i = sorted; i < idx->count; i++)
        if (!strcmp(idx->pkgs[i].path, path))
            return &idx->pkgs[i];
    return NULL;
}

static pidx_pkg_t *add_pkg(pidx_t *idx) {
    if (idx->count == PIDX_MAX_PKGS)
        return NULL;
    if (idx->count == idx->cap) {
        int cap = idx->cap ? idx->cap * 2 : 64;
        pidx_pkg_t *p = realloc(idx->pkgs, (size_t)cap * sizeof(*p));
        if (!p)
            return NULL;
        idx->pkgs = p;
        idx->cap = cap;
    }
    return &idx->pkgs[idx->count++];
}

static void fill_pkg(pidx_pkg_t *p, const char *path, const struct stat *st, pidx_read_fn read) {
    memset(p, 0, sizeof(*p));
    snprintf(p->path, sizeof(p->path), "%s", path);
    p->size = (uint64_t)st->st_size;
    p->mtime = (int64_t)st->st_mtime;
    read(p);
    p->seen = 1;
}

static void visit_pkg(pidx_t *idx, int sorted, const char *path, const struct stat *st, pidx_read_fn read) {
    pidx_pkg_t *p = find_pkg(idx, sorted, path);
    /* also a pkg already seen in this scan: a folder inside another one */
    if (p && p->size == (uint64_t)st->st_size && p->mtime == (int64_t)st->st_mtime) {
        p->seen = 1;
        return;
    }
    if (!p)
        p = add_pkg(idx);
    if (p)
        fill_pkg(p, path, st, read);
    else
        idx->skipped++;
}

static void walk(pidx_t *idx, int sorted, const char *dir, int depth, pidx_read_fn read) {
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        char path[PATH_MAX];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        /* a drive can hold hundreds of thousands of files: asking for each one takes minutes,
         * so only pkgs are asked about when the listing already gives the type */
        if (e->d_type == DT_LNK || (e->d_type == DT_REG && !is_pkg_name(e->d_name)))
            continue;
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= sizeof(path))
            continue;
        if (e->d_type == DT_DIR) {
            if (depth < PIDX_MAX_DEPTH && !skip_folder(e->d_name))
                walk(idx, sorted, path, depth + 1, read);
            continue;
        }
        if (lstat(path, &st))
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < PIDX_MAX_DEPTH && !skip_folder(e->d_name))
                walk(idx, sorted, path, depth + 1, read);
        } else if (S_ISREG(st.st_mode) && is_pkg_name(e->d_name)) {
            visit_pkg(idx, sorted, path, &st, read);
        }
    }
    closedir(d);
}

int pidx_scan(pidx_t *idx, const char *root, const char (*folders)[128], int nfolders, pidx_read_fn read) {
    char dir[PATH_MAX];
    int sorted = idx->count;
    idx->skipped = 0;
    snprintf(idx->root, sizeof(idx->root), "%s", root);
    for (int i = 0; i < idx->count; i++)
        idx->pkgs[i].seen = 0;
    for (int i = 0; i < nfolders; i++) {
        if ((size_t)snprintf(dir, sizeof(dir), "%s%s%s", root, folders[i][0] ? "/" : "", folders[i]) < sizeof(dir))
            walk(idx, sorted, dir, 0, read);
    }
    if ((size_t)snprintf(dir, sizeof(dir), "%s/ps4", root) < sizeof(dir))
        walk(idx, sorted, dir, 0, read);
    int kept = 0;
    for (int i = 0; i < idx->count; i++)
        if (idx->pkgs[i].seen) {
            if (kept != i)
                idx->pkgs[kept] = idx->pkgs[i];
            kept++;
        }
    idx->count = kept;
    if (kept)
        qsort(idx->pkgs, (size_t)kept, sizeof(idx->pkgs[0]), path_cmp);
    return kept;
}

void pidx_free(pidx_t *idx) {
    free(idx->pkgs);
    memset(idx, 0, sizeof(*idx));
}

void pidx_read_pkg(pidx_pkg_t *p) {
    pkg_t *pkg = malloc(sizeof(*pkg));
    sfo_t *sfo = malloc(sizeof(*sfo));
    if (!pkg || !sfo || pkg_open(pkg, p->path)) {
        free(pkg);
        free(sfo);
        return;
    }
    size_t len;
    uint8_t *data = pkg_entry(pkg, 0x1000, &len);
    if (data && !sfo_parse(data, len, sfo)) {
        const sfo_item_t *cat = sfo_get(sfo, "CATEGORY");
        int kind = !cat || cat->is_int ? PIDX_OTHER
                   : !strcmp(cat->sval, "gd") ? PIDX_GAME
                   : !strcmp(cat->sval, "gp") ? PIDX_UPDATE
                   : !strcmp(cat->sval, "ac") ? PIDX_ADDON
                                              : PIDX_OTHER;
        if (kind != PIDX_OTHER && !strncmp(pkg->title_id, "CUSA", 4) && pkg_fake_key(pkg)) {
            const sfo_item_t *ver = sfo_get(sfo, "APP_VER");
            if (!ver)
                ver = sfo_get(sfo, "VERSION");
            p->kind = kind;
            p->incomplete = p->size < pkg->pkg_size;
            snprintf(p->title_id, sizeof(p->title_id), "%s", pkg->title_id);
            snprintf(p->content_id, sizeof(p->content_id), "%s", pkg->content_id);
            if (ver && !ver->is_int)
                snprintf(p->ver, sizeof(p->ver), "%.7s", ver->sval);
        }
    }
    free(data);
    pkg_close(pkg);
    free(pkg);
    free(sfo);
}

static int title_cmp(const void *a, const void *b) {
    return strcmp(((const pidx_game_t *)a)->title_id, ((const pidx_game_t *)b)->title_id);
}

/* An update kept aside in <root>/ps4/<TID>/replaced is not in use. */
static int kept_aside(const pidx_t *idx, const char *path) {
    size_t n = strlen(idx->root);
    if (strncmp(path, idx->root, n) || strncmp(path + n, "/ps4/", 5))
        return 0;
    const char *s = strchr(path + n + 5, '/');
    return s && !strncmp(s, "/replaced/", 10);
}

static int usable(const pidx_t *idx, const pidx_pkg_t *p) {
    return p->kind != PIDX_OTHER && !p->incomplete && !p->ignored && !kept_aside(idx, p->path);
}

/* Pkgs come sorted by path: an equal version keeps the first one. */
static int newer(const pidx_t *idx, int candidate, int current) {
    return current < 0 || strcmp(idx->pkgs[candidate].ver, idx->pkgs[current].ver) > 0;
}

int pidx_group(const pidx_t *idx, pidx_game_t *out, int max) {
    int n = 0;
    for (int i = 0; i < idx->count; i++) {
        const pidx_pkg_t *p = &idx->pkgs[i];
        if (!usable(idx, p))
            continue;
        pidx_game_t *g = NULL;
        for (int k = 0; k < n && !g; k++)
            if (!strcmp(out[k].title_id, p->title_id))
                g = &out[k];
        if (!g) {
            if (n == max)
                continue;
            g = &out[n++];
            memset(g, 0, sizeof(*g));
            snprintf(g->title_id, sizeof(g->title_id), "%s", p->title_id);
            g->game = g->update = -1;
        }
        if (p->kind == PIDX_ADDON) {
            g->addons++;
        } else if (p->kind == PIDX_UPDATE) {
            if (newer(idx, i, g->update))
                g->update = i;
        } else {
            g->copies += g->game >= 0;
            if (newer(idx, i, g->game))
                g->game = i;
        }
    }
    if (n)
        qsort(out, (size_t)n, sizeof(out[0]), title_cmp);
    return n;
}

int pidx_resolve(const pidx_t *idx, const char *tid, const char *legacy_app, const char *legacy_patch, char *app,
                 char *patch, size_t n) {
    int game = -1, update = -1, current = -1;
    for (int i = 0; i < idx->count; i++) {
        const pidx_pkg_t *p = &idx->pkgs[i];
        if (strcmp(p->title_id, tid) || !usable(idx, p))
            continue;
        if (p->kind == PIDX_GAME && newer(idx, i, game))
            game = i;
        if (p->kind == PIDX_UPDATE && newer(idx, i, update))
            update = i;
        if (p->kind == PIDX_UPDATE && !strcmp(p->path, legacy_patch))
            current = i;
    }
    struct stat st;
    int organized = !stat(legacy_app, &st);
    snprintf(app, n, "%s", organized || game < 0 ? legacy_app : idx->pkgs[game].path);
    /* an equal version keeps the update in use: no reason to add the game again */
    int use_update = update >= 0 && (current < 0 || strcmp(idx->pkgs[update].ver, idx->pkgs[current].ver) > 0);
    snprintf(patch, n, "%s", use_update ? idx->pkgs[update].path : legacy_patch);
    return organized || game >= 0 ? 0 : -1;
}
