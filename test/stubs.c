/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The app_* calls of the web panel, recorded instead of done, for the host tests. */
#include <stdio.h>
#include <string.h>

#include "stubs.h"
#include "web.h"

char g_stub_call[64], g_stub_arg[4096];
int g_stub_calls;
size_t g_stub_dlc_len;

static int record(const char *call, const char *arg, char *msg, size_t cap) {
    snprintf(g_stub_call, sizeof(g_stub_call), "%s", call);
    snprintf(g_stub_arg, sizeof(g_stub_arg), "%s", arg ? arg : "(null)");
    g_stub_calls++;
    snprintf(msg, cap, "done \"%s\"", call);
    return 0;
}

void app_games_json(buf_t *out) { buf_printf(out, "{\"version\":\"test\",\"games\":[]}"); }
int app_add(const char *tid, char *msg, size_t cap) { return record("add", tid, msg, cap); }
int app_remove(const char *tid, char *msg, size_t cap) { return record("remove", tid, msg, cap); }
void app_rescan(void) {
    char m[8];
    record("rescan", NULL, m, sizeof(m));
}
int app_stop(char *msg, size_t cap) { return record("stop", NULL, msg, cap); }
int app_stop_open(char *name, size_t cap) {
    snprintf(name, cap, "Game \"A\"");
    return 1;
}
int app_restore(const char *tids, char *msg, size_t cap) { return record("restore", tids, msg, cap); }
void app_config_json(buf_t *out) { buf_printf(out, "{\"batch\":true}"); }
int app_config_set(const char *body, char *msg, size_t cap) { return record("config", body, msg, cap); }
int app_icon(const char *tid, buf_t *out) {
    char m[8];
    record("icon", tid, m, sizeof(m));
    buf_put(out, "PNG", 3);
    return 0;
}
int app_game_json(const char *tid, buf_t *out, char *msg, size_t cap) {
    record("game", tid, msg, cap);
    buf_printf(out, "{\"title_id\":\"%s\",\"updates\":[],\"dlcs\":[]}", tid);
    return 0;
}
int app_choose_update(const char *tid, const char *rel, char *msg, size_t cap) {
    char arg[256];
    snprintf(arg, sizeof(arg), "%s %s", tid, rel);
    return record("update", arg, msg, cap);
}
int app_choose_dlc(const char *tid, const char *on, char *msg, size_t cap) {
    char arg[4096];
    snprintf(arg, sizeof(arg), "%s %s", tid, on);
    g_stub_dlc_len = strlen(on);
    return record("dlc", arg, msg, cap);
}
int app_launch(const char *tid, int force, char *msg, size_t cap) {
    char arg[32];
    snprintf(arg, sizeof(arg), "%s %d", tid, force);
    record("launch", arg, msg, cap);
    if (!strcmp(tid, "CUSA00002") && !force) {
        snprintf(msg, cap, "Uncharted \"4\"");
        return 1;
    }
    return 0;
}
