/* SPDX-License-Identifier: TBD */
#include "../svm_lib.h"

const unsigned long ro_probe __attribute__((section(".probe_ro"), used)) = 0x12345678;
extern void ro_store(volatile unsigned long *p, unsigned long value);
extern void xn_probe(void);

__asm__(".section .probe_store,\"ax\"\n"
        ".global ro_store\nro_store:\n str x1, [x0]\n ret\n"
        ".section .probe_xn,\"ax\"\n"
        ".global xn_probe\nxn_probe:\n nop\n b xn_entered\n"
        ".previous\n");

void xn_entered(void)
{
    svm_puts("SVM: s2perm XN ENTERED\n");
}

void SVM_ENTRY _start(void)
{
    unsigned long sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    if (sctlr & 1) {
        svm_puts("SVM: s2perm MMU ON\n");
        svm_done(1);
    }
#if S2PERM_PROBE == 0
    if (*(volatile const unsigned long *)&ro_probe != 0x12345678) {
        svm_puts("SVM: s2perm RO BAD DATA\n");
        svm_done(1);
    }
    svm_puts("SVM: s2perm RO read ok\n");
    ro_store((volatile unsigned long *)&ro_probe, 0xdeadbeef);
    svm_puts("SVM: s2perm RO STORE SURVIVED\n");
#else
    if (*(volatile const unsigned int *)xn_probe != 0xd503201fU) {
        svm_puts("SVM: s2perm XN BAD DATA\n");
        svm_done(1);
    }
    svm_puts("SVM: s2perm XN read ok\n");
    xn_probe();
    svm_puts("SVM: s2perm XN RETURNED\n");
#endif
    svm_done(1);
}
