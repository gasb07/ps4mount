#!/bin/bash
# Builds build/ps4mount.elf with the PS5 Payload SDK (https://github.com/ps5-payload-dev/sdk).
# Set PS5_PAYLOAD_SDK when the SDK is not in /opt/ps5-payload-sdk.
set -e
cd "$(dirname "$0")"
B=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/bin
mkdir -p build
# Embedded data: panel page, Media tab icon, playgo record templates.
{
    (cd web && xxd -i -n index_html index.html)
    (cd web && xxd -i -n legacy_html legacy.html)
} > build/page_data.h
(cd assets && xxd -i -n icon0_png icon0.png) > build/icon_data.h
{
    (cd tools && xxd -i -n k_pdb_template pdb_app_template.bin)
    (cd tools && xxd -i -n k_pdb_patch_template pdb_patch_template.bin)
    (cd tools && xxd -i -n k_pdb_addcont_template pdb_addcont_template.bin)
} > build/pdb_template.h
[ -f build/sqlite3.o ] || $B/prospero-clang -O2 -w -c third_party/sqlite/sqlite3.c -o build/sqlite3.o \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0
$B/prospero-clang -O2 -Wall -Wextra -Ibuild -Ithird_party/sqlite -o build/ps4mount.elf \
    src/main.c src/fw.c src/logts.c src/web.c src/pkg.c src/build.c src/db.c src/origin.c src/crypto.c src/logfile.c build/sqlite3.o \
    -lSceUserService -lSceAppInstUtil -lSceSystemService -lkernel_sys
ls -la build/ps4mount.elf
