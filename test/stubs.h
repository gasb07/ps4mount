/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Last app_* call made by the web panel in the host tests. */
#pragma once

#include <stddef.h>

extern char g_stub_call[64], g_stub_arg[4096];
extern int g_stub_calls;
extern size_t g_stub_dlc_len; /* length of the last add-on list, which g_stub_arg may cut */
