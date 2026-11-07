/* SPDX-License-Identifier: TBD */
/*
 * SVM case "preempt" (M11 slice D): time-slice preemption on a SHARED pCPU.
 *
 * Profile svm_ctxsw: both vCPUs of VM0 sit on pCPU0. vCPU0 CPU_ONs vCPU1 and
 * then spins waiting for its flag; neither side executes WFI, so no
 * cooperative yield can ever bring vCPU1 onto the pCPU. Only the EL2 10 ms
 * tick can take the pCPU away from vCPU0 — the H-174 shared-pCPU starvation
 * shape (ADR-0019), with caches and console left out.
 *
 * Timeline: vCPU0 spins -> tick #1 takes the pCPU -> vCPU1 spins, prints its
 * marker, raises the go flag, keeps spinning -> tick #2 takes the pCPU ->
 * vCPU0 sees the flag and checks its GPR canaries survived two involuntary
 * switches (the preemption path discards the guest context via hv_restore,
 * so resume has to rebuild it) -> DONE 0.
 *
 * The go flag is raised AFTER vCPU1's print: the UART is shared, so raising
 * it first lets vCPU0's line interleave into vCPU1's.
 *
 * Without the tick vCPU0 exhausts its spin budget and reports that vCPU1
 * never ran: the controlled red in
 * docs/debug/2026-11-07-m11-d-preempt-controlled-red.md.
 *
 * Run: make test-qemu-preempt   (in the `test` aggregate since slice D)
 */
#include "../svm_lib.h"

/* Keep in sync with arch/psci.h (test guest has no access to hv headers). */
#define PSCI_CPU_ON_64  0xC4000003UL

#define V0_SPINS  20000000UL   /* >> one 10 ms quantum of TCG guest time */
#define V1_SPINS  200000UL

#define CANARY(i) ((unsigned long)0xD11E0000UL + (i))

static volatile int v1_ran;
static volatile unsigned char v1_stack[0x4000] __attribute__((aligned(16), used));

/* Compute-bound by design: after the marker, vCPU0 still has to be preempted
 * a second time to get the pCPU back. `yield` is a HINT, not a trap. */
static void v1_main(void) __attribute__((used, noreturn));
static void v1_main(void)
{
    __asm__ volatile("msr daifclr, #2");
    for (unsigned long i = 0; i < V1_SPINS; i++)
        __asm__ volatile("yield");
    svm_puts("PREEMPT: v1 ran\n");
    v1_ran = 1;
    for (;;)
        __asm__ volatile("yield");
}

extern char preempt_secondary_entry[];    /* asm label below */
__asm__(
    ".global preempt_secondary_entry\n"
    "preempt_secondary_entry:\n"
    "   msr daifset, #0xf\n"
    "   ldr x0, =v1_stack + 0x4000\n"
    "   mov sp, x0\n"
    "   b v1_main\n"
);

void SVM_ENTRY _start(void)
{
    /* Register-asm locals: the compiler tracks them, so it cannot allocate
     * its own values over the canaries (same idiom as ctxsw.c). */
    register unsigned long r19 asm("x19") = CANARY(0);
    register unsigned long r20 asm("x20") = CANARY(1);
    register unsigned long r21 asm("x21") = CANARY(2);
    register unsigned long r22 asm("x22") = CANARY(3);
    register unsigned long r23 asm("x23") = CANARY(4);

    __asm__ volatile("msr daifclr, #2");

    unsigned long ret = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                                 (unsigned long)preempt_secondary_entry, 0UL);
    if (ret != 0) {
        svm_puts("PREEMPT: CPU_ON failed\n");
        svm_done(1);
    }

    unsigned long spins = V0_SPINS;
    while (!v1_ran && spins-- > 0)
        __asm__ volatile("yield");
    if (!v1_ran) {
        svm_puts("PREEMPT: v1 never ran\n");
        svm_done(1);
    }

    if (r19 != CANARY(0) || r20 != CANARY(1) || r21 != CANARY(2) ||
        r22 != CANARY(3) || r23 != CANARY(4)) {
        svm_puts("PREEMPT: v0 canary corrupt\n");
        svm_done(1);
    }
    svm_puts("PREEMPT: v0 observed v1\n");
    svm_done(0);
}
