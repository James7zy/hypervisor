/* SPDX-License-Identifier: TBD */
#include "../svm_lib.h"

/* One fatal access per boot; VM1's vCPU0 shares pCPU0 and must finish. */
#if S2_BOUNDS_PROBE == 0
#define PROBE_IPA 0x40080000UL /* HV image below the SVM RAM window. */
#elif S2_BOUNDS_PROBE == 1
#define PROBE_IPA 0x40400000UL /* First byte beyond the SVM RAM window. */
#elif S2_BOUNDS_PROBE == 2
#define PROBE_IPA 0xc0200000UL /* Peer backing PA used as IPA: regression only. */
#else
#error Invalid S2_BOUNDS_PROBE
#endif

void SVM_ENTRY _start(void)
{
    svm_puts("SVM: s2bounds vm0 reading unauthorized IPA\n");
    (void)*(volatile unsigned long *)PROBE_IPA;
    svm_puts("SVM: s2bounds SURVIVED\n");
    svm_done(0);
}
