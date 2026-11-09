#!/bin/sh
# SPDX-License-Identifier: TBD
# Fixed-string positive and optional forbidden markers, independent of QEMU.
set -eu
EXPECT=${1:?usage: check_svm_output.sh <expect-file> [forbid-file]}
FORBID=${2:-}
[ -f "${EXPECT}" ] || { echo "ERROR: ${EXPECT} not found."; exit 1; }
[ -z "${FORBID}" ] || [ -f "${FORBID}" ] || {
    echo "ERROR: ${FORBID} not found."; exit 1;
}
OUTPUT=$(cat)
FAILURES=0
while IFS= read -r line; do
    [ -n "${line}" ] || continue
    if printf '%s\n' "${OUTPUT}" | grep -qF -- "${line}"; then
        printf "PASS: '%s'\n" "${line}"
    else
        printf "FAIL: '%s' not found in output\n" "${line}"
        FAILURES=$((FAILURES + 1))
    fi
done < "${EXPECT}"

if [ -n "${FORBID}" ]; then
    while IFS= read -r line; do
        [ -n "${line}" ] || continue
        if printf '%s\n' "${OUTPUT}" | grep -qF -- "${line}"; then
            printf "FAIL: forbidden '%s' found in output\n" "${line}"
            FAILURES=$((FAILURES + 1))
        else
            printf "PASS: forbidden '%s' absent\n" "${line}"
        fi
    done < "${FORBID}"
fi

if [ "${FAILURES}" -gt 0 ]; then
    printf "\n--- QEMU output ---\n%s\n---\n" "${OUTPUT}"
    exit 1
fi
printf "ALL PASS\n"
