/* SPDX-License-Identifier: TBD */
#include "../svm_lib.h"

void SVM_ENTRY _start(void)
{
    unsigned long freq, before, now;
    unsigned int progress = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    for (;;) {
        char line[] = "SVM: s2perm survivor vm=1 vcpu=0 pcpu=0 progress=00000000\n";
        const char hex[] = "0123456789abcdef";
        ++progress;
        for (unsigned int i = 0; i < 8; i++)
            line[sizeof(line) - 3 - i] = hex[(progress >> (4 * i)) & 15];
        svm_puts(line);
        __asm__ volatile("mrs %0, cntpct_el0" : "=r"(before));
        do {
            __asm__ volatile("mrs %0, cntpct_el0" : "=r"(now));
        } while (now - before < freq / 20);
    }
}
