#!/bin/bash
# Builds and runs the host tests (no PS5 or SDK needed): bash test/run.sh
#   CC=clang              compiler (default: cc)
#   SANITIZE=1            with AddressSanitizer and UndefinedBehaviorSanitizer
set -e
cd "$(dirname "$0")/.."
B=build/test
mkdir -p "$B"
CC=${CC:-cc}
FLAGS="-O1 -g -Wall -Wextra -I$B -Isrc"
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS="$FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
    export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1
fi

# Embedded data, as build.sh makes it
{
    (cd tools && xxd -i -n k_pdb_template pdb_app_template.bin)
    (cd tools && xxd -i -n k_pdb_patch_template pdb_patch_template.bin)
    (cd tools && xxd -i -n k_pdb_addcont_template pdb_addcont_template.bin)
} | sed 's/^unsigned/static const unsigned/' > "$B/pdb_template.h"  # included by build.c and unit.c
{
    (cd web && xxd -i -n index_html index.html)
    (cd web && xxd -i -n legacy_html legacy.html)
} > "$B/page_data.h"

# SQLite once per compiler and flags (it is large)
SQ="$B/sqlite3-$(echo "$CC $FLAGS" | md5sum | cut -c1-8).o"
[ -f "$SQ" ] || $CC $FLAGS -w -c third_party/sqlite/sqlite3.c -o "$SQ" \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0

echo "== unit"
$CC $FLAGS -o "$B/unit" test/unit.c src/build.c src/crypto.c src/pkg.c src/origin.c
"$B/unit"

echo "== firmware simulation"
$CC $FLAGS -o "$B/fw_sim" test/fw_sim.c src/fw.c
"$B/fw_sim"

echo "== system database"
$CC $FLAGS -Ithird_party/sqlite -DAPP_DB="\"$B/app.db\"" -o "$B/db_test" test/db_test.c src/db.c src/crypto.c "$SQ"
"$B/db_test"

echo "== web panel server"
$CC $FLAGS -Itest -o "$B/http_test" test/http_test.c test/stubs.c src/web.c src/build.c src/crypto.c -lpthread
"$B/http_test"

echo "== log with times"
$CC $FLAGS -o "$B/log_test" test/log_test.c src/logts.c -lpthread
"$B/log_test"

echo "== pkg index"
$CC $FLAGS -o "$B/pkgindex_test" test/pkgindex_test.c src/pkgindex.c src/pkg.c src/crypto.c
"$B/pkgindex_test"

echo "== add-on copies"
$CC $FLAGS -o "$B/accopy_test" test/accopy_test.c src/accopy.c
"$B/accopy_test"

echo "== repeated klog lines"
$CC $FLAGS -o "$B/klogdup_test" test/klogdup_test.c src/klogdup.c
"$B/klogdup_test"
