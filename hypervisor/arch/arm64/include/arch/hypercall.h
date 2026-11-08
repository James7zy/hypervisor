/* SPDX-License-Identifier: TBD */
#ifndef HV_HYPERCALL_H
#define HV_HYPERCALL_H

/* The hypercall ABI itself (ids, error codes, dispatch) is generic — see
 * <hypercall.h>. This header is what the arm64 trap entry includes: the ABI
 * plus the SMCCC constants only this port answers. */
#include <hypercall.h>

#define SMCCC_NOT_SUPPORTED  (~0ULL)

/* SMCCC v1.x base functions (ARM DEN 0028 A). Answered in handle_hvc before
 * the PSCI dispatch: their service ranges overlap the 0x80/0xC0 calling
 * convention prefixes. */
#define SMCCC_VERSION           0x80000000U
#define SMCCC_ARCH_FEATURES     0x80000001U
#define SMCCC_VERSION_1_1       0x00010001U   /* major<<16 | minor */

#endif /* HV_HYPERCALL_H */
