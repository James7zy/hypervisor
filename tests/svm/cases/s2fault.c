/* SPDX-License-Identifier: TBD */
/*
 * SVM case "s2fault" (HV-011/HV-012 T3): a guest's own Stage-2 fault must stop
 * that vCPU only, never the pCPU.
 *
 * VM0's vCPU0 reads IPA 0x80000000 — one 1 GB L1 block past the RAM window, so
 * there is no mapping and no MMIO handler behind it. EL2 takes an EC=0x24 data
 * abort, logs `[hv] s2 fault: ...` and parks the vCPU (VCPU_EV_OFF + hv_restore).
 *
 * The scenario runs on the 2x2-on-2 profile (CONFIG_SVM_XVM2): VM1's vCPU0 is
 * affined to the SAME pCPU0 as VM0's vCPU0, so VM1's "hello from VM1" (from
 * svm-vm1.bin) can only appear if the pCPU kept running and picked the other
 * VM after VM0 was parked. With the old cpu_arch_halt() the pCPU dies here and
 * VM1 never boots.
 *
 * Run: make test-qemu-s2fault   (included in the `test` aggregate)
 */
#include "../svm_lib.h"

void SVM_ENTRY _start(void)
{
    volatile unsigned long *unmapped = (volatile unsigned long *)0x80000000UL;

    svm_puts("SVM: s2fault vm0 reading unmapped IPA\n");
    (void)*unmapped;   /* Stage-2 translation fault → park this vCPU */
    svm_puts("SVM: s2fault vm0 SURVIVED THE FAULT\n");
    svm_done(0);
}
