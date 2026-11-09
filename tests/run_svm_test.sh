#!/bin/sh
# SPDX-License-Identifier: TBD
# Integration test: boot the hypervisor with one or two bare-metal SVM guests
# in QEMU and check that every line of an expect file appears in the output
# (fixed-string match, order-independent). Optional SVM_FORBID names forbidden markers.
#
# Usage: sh tests/run_svm_test.sh tests/svm/expect/<scenario>.txt
# Called by: make test-qemu / test-qemu-vtimer / test-qemu-dual
# Requires: SVM_BIN (and SVM_BIN2 for dual), HYPERVISOR_ELF set by Makefile,
#           qemu-system-aarch64 in PATH.
set -eu

EXPECT=${1:?usage: run_svm_test.sh <expect-file>}
: "${SVM_BIN:?must be set by make}"
[ -f "${EXPECT}" ] || { echo "ERROR: ${EXPECT} not found."; exit 1; }

TIMEOUT=10

OUTPUT=$(timeout "${TIMEOUT}" ./scripts/run-qemu.sh </dev/null 2>&1 || true)

printf '%s\n' "${OUTPUT}" | sh tests/check_svm_output.sh "${EXPECT}" "${SVM_FORBID:-}"
