#!/bin/bash
# Rules the PS4Mount sources must keep, checked on every change: bash test/policy.sh
# Each rule comes from a problem seen on consoles (see the comments).
cd "$(dirname "$0")/.." || exit 1
fail=0
rule() { echo "::error::$1"; fail=1; }
SRC=(src/*.c src/*.h)
OTHER=() # every source but main.c
for f in src/*.c; do [ "$f" = src/main.c ] || OTHER+=("$f"); done

# The system databases are written by the system alone: rows written from outside made
# ShellCore flag app.db as corrupted (0.3.3), and read locks made its writes fail (0.3.4).
grep -nE 'sqlite3_(exec|blob_open|backup_init)\b' "${SRC[@]}" && rule "sqlite3 calls that can write: the system databases are read-only for PS4Mount"
grep -nE 'SQLITE_OPEN_(READWRITE|CREATE)' "${SRC[@]}" && rule "a database opened for writing"
grep -nE 'sqlite3_prepare' "${SRC[@]}" | grep -vE 'sqlite3_prepare(_v2)?\([^,]+, "SELECT ' && rule "SQL other than SELECT"
grep -n 'sqlite3_open' "${SRC[@]}" | grep -v 'mode=ro&nolock=1' && rule "every sqlite3_open must use mode=ro&nolock=1"
grep -nE '"/system_data/priv/mms/[^"]*"' "${SRC[@]}" | grep -vE 'APP_DB|db_mtime|static const char \*files|"/system_data/priv/mms/[a-z]+\.db(-journal)?"' &&
    rule "a path into the system database folder outside the read-only uses"
for f in "${SRC[@]}"; do
    awk '/write_file\(|open\([^)]*O_(WRONLY|RDWR)/ && /priv\/mms/ {print FILENAME":"NR": "$0; bad=1} END {exit bad}' "$f" ||
        rule "a write into /system_data/priv/mms"
done

# No shell, no other programs: the payload has no business running commands.
grep -nE '\b(system|popen|execl|execlp|execle|execv|execvp|execve)\s*\(' "${SRC[@]}" && rule "running commands"

# Unbounded string functions on data from files or the network.
grep -nE '\b(strcpy|strcat|sprintf|gets|vsprintf)\s*\(' "${SRC[@]}" && rule "unbounded string function (use snprintf/memcpy with a size)"

# Closing apps is only for moving pkgs back (close_open_games): nowhere else.
n=$(grep -c 'sceSystemServiceKillApp(' src/main.c)
[ "$n" -le 2 ] || rule "sceSystemServiceKillApp used outside close_open_games ($n uses)"
grep -nE 'sceSystemServiceKillApp|sceSystemServiceLaunchApp|sceLncUtil' "${OTHER[@]}" && rule "launching or closing apps outside main.c"

# The web panel changes state only on POST: every app_* call that changes something sits in
# a branch that checks for POST.
awk '/strn?cmp\(path/ {cond=$0} /app_(add|remove|restore|config_set|rescan|stop)\(/ && cond !~ /post/ {print FILENAME":"NR": "$0; bad=1} END {exit bad}' src/web.c ||
    rule "a state change in the web panel reachable without POST"

# The page for older firmwares must stay without <dialog> and modern JavaScript.
grep -nE '</dialog>|showModal|fetch\(|=>|\blet |\bconst ' web/legacy.html && rule "legacy.html uses <dialog> or modern JavaScript"

[ $fail = 0 ] && echo "policy: all rules hold"
exit $fail
