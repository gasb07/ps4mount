/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Fuzz target: origin.txt (a file on the drive). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "origin.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static char dir[64];
    if (!dir[0]) {
        snprintf(dir, sizeof(dir), "/tmp/fuzz_origin_%d", (int)getpid());
        mkdir(dir, 0700);
    }
    char path[128], out[4096];
    snprintf(path, sizeof(path), "%s/" ORIGIN_FILE, dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;
    fwrite(data, 1, size, f);
    fclose(f);
    origin_find(dir, "app.pkg", out, sizeof(out));
    origin_find(dir, "addcont/CUSA00001/LABEL/ac.pkg", out, sizeof(out));
    return 0;
}
