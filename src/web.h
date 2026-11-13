/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Web panel interface. */
#pragma once

#include "build.h"

#define WEB_PORT 8770

/* Implemented by main.c for the web panel. */
void app_games_json(buf_t *out);
int app_add(const char *tid, char *msg, size_t cap);
int app_remove(const char *tid, char *msg, size_t cap);
void app_rescan(void);
/* Unmounts every game and stops PS4Mount (from the main loop). -1 with the reason in msg
 * when a game is open. */
int app_stop(char *msg, size_t cap);
/* 1 with the name of the open game (the panel says to close it before stopping), else 0. */
int app_stop_open(char *name, size_t cap);
/* Closes the open game and moves the pkgs of the games in tids (comma separated title ids)
 * or of all games (NULL) back to where they came from. */
int app_restore(const char *tids, char *msg, size_t cap);
/* Forgets the pkgs moved back, so the scan picks them up again. */
void app_config_json(buf_t *out);
/* body: form-encoded settings; 0 when saved, msg says why not. */
int app_config_set(const char *body, char *msg, size_t cap);
/* Updates and add-ons of one game, for the panel's dialogs. 0 on success. */
int app_game_json(const char *tid, buf_t *out, char *msg, size_t cap);
/* Puts the update rel ("replaced/<name>") in use; the game is added again. */
int app_choose_update(const char *tid, const char *rel, char *msg, size_t cap);
/* Keeps on only the add-ons in on (content IDs, comma separated); the game is added again. */
int app_choose_dlc(const char *tid, const char *on, char *msg, size_t cap);
/* Starts the game. 1 when another game is open and force is 0 (msg: its name); with
 * force that game is closed first, without saving. */
int app_launch(const char *tid, int force, char *msg, size_t cap);
/* Fills an icon0.png for tid; 0 on success. */
int app_icon(const char *tid, buf_t *out);

int web_listen(void);
/* Serves one connection on the listening socket. */
void web_serve(int listen_fd);
/* Answers the request on a connected socket and closes it. */
void web_handle(int fd);
