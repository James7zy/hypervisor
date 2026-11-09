#!/bin/sh
# SPDX-License-Identifier: TBD
set -eu
TMP=$(mktemp -d)
trap 'rm -rf "${TMP}"' EXIT HUP INT TERM
printf 'ready [0]\ndone\n' > "${TMP}/expect"
printf 'SURVIVED\n' > "${TMP}/forbid"
printf 'ready [0]\ndone\n' > "${TMP}/good"
printf 'ready [0]\ndone\nSURVIVED\n' > "${TMP}/bad"
sh tests/check_svm_output.sh "${TMP}/expect" "${TMP}/forbid" < "${TMP}/good" > /dev/null
# All positive expectations exist: the forbidden marker alone must fail.
if sh tests/check_svm_output.sh "${TMP}/expect" "${TMP}/forbid" < "${TMP}/bad" > /dev/null; then
    echo 'FAIL: forbidden marker was accepted'; exit 1
fi
# Default behavior stays positive-only.
sh tests/check_svm_output.sh "${TMP}/expect" < "${TMP}/bad" > /dev/null
if printf 'ready [0]\n' | sh tests/check_svm_output.sh "${TMP}/expect" > /dev/null; then
    echo 'FAIL: missing positive marker was accepted'; exit 1
fi
if sh tests/check_svm_output.sh "${TMP}/expect" "${TMP}/missing" < "${TMP}/good" > /dev/null; then
    echo 'FAIL: missing forbid file was accepted'; exit 1
fi
echo 'check_svm_output PASS (5 checks)'
