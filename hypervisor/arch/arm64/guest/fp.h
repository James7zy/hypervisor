/* SPDX-License-Identifier: TBD */
#ifndef HV_ARCH_GUEST_FP_H
#define HV_ARCH_GUEST_FP_H

#include <vm.h>   /* struct fp_state */

/* Eager guest FP/SIMD context switch (fp.S, ADR-0020). */
void fp_save(struct fp_state *s);
void fp_restore(const struct fp_state *s);

#endif /* HV_ARCH_GUEST_FP_H */
