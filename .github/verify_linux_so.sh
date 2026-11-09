#!/usr/bin/env bash
# Sanity checks for built l4fixes_vsp.so files.
# Usage: MAX_GLIBC=2.39 bash .github/verify_linux_so.sh <file.so>...
set -u
MAX_GLIBC="${MAX_GLIBC:-2.39}"
fail=0

err() { echo "::error::$*"; fail=1; }

for so in "$@"; do
    echo "== $so"
    if [ ! -f "$so" ]; then err "$so: not found"; continue; fi

    file "$so" | grep -q 'ELF 32-bit' || err "$so: not a 32-bit ELF (srcds is x86)"

    # srcds puts its own ancient libstdc++.so.6 (CXXABI 1.3.5) first in the library path.
    readelf -d "$so" | grep 'NEEDED' | grep -q 'libstdc++' && err "$so: links libstdc++ dynamically (use -static-libstdc++)"

    nm -D --defined-only "$so" | grep -qw CreateInterface || err "$so: CreateInterface is not exported"

    need=$(objdump -T "$so" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -Vu | tail -1)
    echo "   requires glibc $need (max allowed $MAX_GLIBC)"
    newest=$(printf '%s\n%s\n' "$need" "$MAX_GLIBC" | sort -V | tail -1)
    [ "$newest" = "$MAX_GLIBC" ] || err "$so: requires glibc $need > $MAX_GLIBC"
done

[ $fail -eq 0 ] && echo "All checks passed."
exit $fail
