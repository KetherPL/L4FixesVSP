#!/usr/bin/env bash
# Sanity checks for built l4fixes_vsp.so files.
# Usage: RUNTIME_LINKAGE=static|shared MAX_GLIBC=2.39 bash .github/verify_linux_so.sh <file.so>...
set -u
MAX_GLIBC="${MAX_GLIBC:-2.39}"
RUNTIME_LINKAGE="${RUNTIME_LINKAGE:-static}"
case "$RUNTIME_LINKAGE" in
    static|shared) ;;
    *) echo "::error::RUNTIME_LINKAGE must be static or shared"; exit 2 ;;
esac
fail=0

err() { echo "::error::$*"; fail=1; }

for so in "$@"; do
    echo "== $so"
    if [ ! -f "$so" ]; then err "$so: not found"; continue; fi

    file "$so" | grep -q 'ELF 32-bit' || err "$so: not a 32-bit ELF (srcds is x86)"

    dependencies=$(readelf -d "$so" | grep 'NEEDED')
    for runtime in libstdc++ libgcc_s; do
        if printf '%s\n' "$dependencies" | grep -Fq "[$runtime.so"; then
            [ "$RUNTIME_LINKAGE" = static ] && err "$so: links $runtime dynamically; expected static runtimes"
        else
            [ "$RUNTIME_LINKAGE" = shared ] && err "$so: missing shared $runtime dependency"
        fi
    done

    nm -D --defined-only "$so" | grep -qw CreateInterface || err "$so: CreateInterface is not exported"

    need=$(objdump -T "$so" | grep -oE 'GLIBC_[0-9.]+' | sed 's/GLIBC_//' | sort -Vu | tail -1)
    echo "   requires glibc $need (max allowed $MAX_GLIBC)"
    newest=$(printf '%s\n%s\n' "$need" "$MAX_GLIBC" | sort -V | tail -1)
    [ "$newest" = "$MAX_GLIBC" ] || err "$so: requires glibc $need > $MAX_GLIBC"
done

[ $fail -eq 0 ] && echo "All checks passed."
exit $fail
