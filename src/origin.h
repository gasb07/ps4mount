/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Where the pkgs came from, so they can be moved back. */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* <game folder>/origin.txt: one "<path in the game folder>\t<original path from the drive
 * root>" line per moved pkg, appended on every move; the last line for a path wins. */
#define ORIGIN_FILE "origin.txt"

/* Records that <game_dir>/<rel> came from <orig> (relative to the drive root). 0 on success. */
int origin_add(const char *game_dir, const char *rel, const char *orig);
/* Original path of <game_dir>/<rel>, relative to the drive root. 0 when recorded. */
int origin_find(const char *game_dir, const char *rel, char *orig, size_t n);

/* List of pkgs moved back, which the scan must not pick up again: one
 * "<path from the drive root>\t<size>" line each. A pkg that was moved, renamed or changed
 * no longer matches and is picked up as usual. */

/* Joins an original path to a free name in its folder: "<root>/<orig>", or with " (1)",
 * " (2)"... before the extension when the name is taken. 0 when a free name was found. */
int origin_free_path(const char *root, const char *orig, char *out, size_t n);
