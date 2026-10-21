/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Log file size limit. */
#pragma once

#include <sys/types.h>

/* When path is larger than max bytes, keeps only its last keep bytes (from the
 * first whole line). Returns 1 when trimmed, 0 when left alone, -1 on error. */
int log_trim(const char *path, off_t max, off_t keep);
