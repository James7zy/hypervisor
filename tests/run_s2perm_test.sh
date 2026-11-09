#!/bin/sh
# SPDX-License-Identifier: TBD
# 保留每次 QEMU 启动的原始输出与 exit；持续运行的 survivor 预期 timeout=124。
set -eu
probe=${1:?probe required}
log=${2:?log path required}
set +e
timeout 10 ./scripts/run-qemu.sh </dev/null >"$log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" >"$log.exit"
if [ "$rc" -ne 124 ]; then
    echo "FAIL: QEMU exit=$rc (expected timeout 124); log=$log"
    exit 1
fi
sh tests/check_svm_output.sh "tests/svm/expect/s2perm-$probe.txt" tests/svm/expect/s2perm-forbid.txt <"$log"
python3 tests/check_s2perm_output.py "$probe" "$log"
