/* SPDX-License-Identifier: TBD */
/* Diagnostic only: QEMU 9.2.4 does not trap WFE. On svm_ctxsw's shared
 * pCPU the queued secondary cannot run: expect an external timeout after
 * CPU_ON, not DONE. See docs/debug/m11-c-survivor-freeze.md. Not a gate.
 * Superseded by the `preempt` gate case (M11 slice D), which covers the same
 * starvation shape with a spin instead of WFE; kept as an independent
 * diagnostic, deliberately NOT in `test`. */
#include "../svm_lib.h"

static volatile int ready;
static unsigned char stack[0x4000] __attribute__((aligned(16), used));
static void secondary(void) __attribute__((used, noreturn));
static void secondary(void)
{
    ready = 1;
    for (;;)
        __asm__ volatile("wfi");
}

extern char wfe_secondary_entry[];
__asm__(
    ".global wfe_secondary_entry\n"
    "wfe_secondary_entry:\n"
    "ldr x0, =stack + 0x4000\n"
    "mov sp, x0\n"
    "b secondary\n"
);

void SVM_ENTRY _start(void)
{
    unsigned long ret = svm_hvc4(0xC4000003UL, 1UL,
                                 (unsigned long)wfe_secondary_entry, 0UL);
    if (ret != 0)
        svm_done(1);
    svm_puts("WFE_WAIT: CPU_ON succeeded; waiting without WFI\n");
    while (!ready)
        __asm__ volatile("wfe");
    svm_puts("WFE_WAIT: secondary ran\n");
    svm_done(0);
}
