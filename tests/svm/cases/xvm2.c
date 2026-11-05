/* SPDX-License-Identifier: TBD */
/*
 * SVM case "xvm2" (M11 slice C): cross-VM detector for the OTHER pCPU.
 *
 * Profile svm_xvm2: VM0 vcpu_pcpu = {0, 1}, VM1 = {0, 1} — BOTH pCPUs are
 * shared, exactly like the 2x2-on-2 Linux demo. The pre-existing "xvm" case
 * only shares pCPU0 and therefore cannot see a survivor stranded on the
 * pCPU whose sibling got swept by the other VM's SYSTEM_OFF.
 *
 * One source, built twice (-DXVM_ID=0/1).
 *
 *   +0    VM0 v0 (pCPU0): CPU_ON vCPU1 -> pCPU1, arms CNTV for +900, wfi
 *         -> BLOCKED. VM1 v0 (pCPU0): CPU_ON vCPU1 -> pCPU1, then busy-waits
 *         (it must be RUNNING when it SYSTEM_OFFs at +700).
 *   +0    VM0 v1 (pCPU1): every 100 ms: arm CNTV, wfi, ticks++.
 *         VM1 v1 (pCPU1): busy-waits to +700, then yields forever.
 *   +700  VM1 v0: SYSTEM_OFF -> both VM1 vCPUs swept to OFF; VM1 v1 is
 *         RUNNING on pCPU1, so it is kicked and unwinds (T6).
 *   +900  VM0 v0's timer fires (its own CNTV, or the EL2 background timer
 *         while it was off-CPU). It samples vCPU1's tick counter, waits on
 *         its own timer to +1200, samples again.
 *   +1200 verdict: vCPU1 — sharing pCPU1 with the killed VM until now —
 *         must have been woken by its own timer at least once after the
 *         sweep. A survivor left stranded on pCPU1 (lost wake / bad
 *         cross-VM resume) shows up as a frozen counter.
 *
 * Run: make test-qemu-xvm2   (in the `test` aggregate)
 */
#include "../svm_lib.h"

#ifndef XVM_ID
#error "build with -DXVM_ID=0 or 1"
#endif

#define PSCI_CPU_ON_64   0xC4000003UL
#define PSCI_SYSTEM_OFF  0x84000008UL

static volatile unsigned long t_start;   /* this VM's time origin */
static volatile unsigned long v1_ticks;  /* VM0: bumped by ITS vCPU1 */
static volatile int timer0;              /* VM0 v0: own vtimer fired */
static volatile unsigned char v1_stack[0x4000] __attribute__((aligned(16), used));

static unsigned long now(void)
{
    unsigned long c;
    __asm__ volatile("isb\n\tmrs %0, CNTVCT_EL0" : "=r"(c) :: "memory");
    return c;
}

static unsigned long ms(unsigned long n)
{
    unsigned long f;
    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(f));
    return n * (f / 1000UL);
}

static void busy_until(unsigned long t)
{
    while (now() < t)
        ;
}

static void check(int cond, const char *what)
{
    svm_puts(cond ? what : "XVM2: CHECK FAILED\n");
}

static unsigned long my_aff0(void)
{
    unsigned long m;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    return m & 0xFFUL;
}

/* The only virtual IRQ any vCPU here takes is its own vtimer PPI 27. */
void svm_irq_handler(void)
{
    unsigned long iar, intid;

    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
    intid = iar & 0xFFFFFFUL;
    if (intid == 27U) {
        /* Mask the virtual timer at once: the level PPI re-raises until the
         * guest deactivates, and this case takes the IRQ on BOTH vCPUs. */
        __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(0UL));
        if (my_aff0() == 0UL)
            timer0++;
        else
            v1_ticks++;
    }
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"(iar));
    __asm__ volatile("msr ICC_DIR_EL1, %0"   :: "r"(iar));
    __asm__ volatile("isb");
}

static void secondary_main(void) __attribute__((used, noreturn));

extern char xvm2_secondary_entry[];
__asm__(
    ".global xvm2_secondary_entry\n"
    "xvm2_secondary_entry:\n"
    "   msr daifset, #0xf\n"
    "   ldr x0, =v1_stack + 0x4000\n"
    "   mov sp, x0\n"
    "   b secondary_main\n"
);

#if XVM_ID == 0
/* VM0 vCPU1 (pCPU1, shares with VM1 vCPU1): keep taking its own timer. */
static void secondary_main(void)
{
    svm_gic_el1_init();
    __asm__ volatile("msr daifclr, #2");
    for (;;) {
        __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(now() + ms(100)));
        __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
        __asm__ volatile("wfi");             /* BLOCKED: own timer wakes it */
    }
}

void SVM_ENTRY _start(void)
{
    t_start = now();
    busy_until(t_start + ms(20));            /* past the boot banners */
    svm_puts("XVM2: start\n");
    unsigned long r = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                               (unsigned long)xvm2_secondary_entry, 0UL);
    check(r == 0, "XVM2: VM0 CPU_ON(vCPU1) == SUCCESS\n");

    svm_gic_el1_init();
    __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(t_start + ms(900)));
    __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
    __asm__ volatile("msr daifclr, #2");
    while (!timer0)
        __asm__ volatile("wfi");             /* BLOCKED until +900 */

    unsigned long ticks_before = v1_ticks;
    __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(t_start + ms(1300)));
    __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
    while (!timer0)
        __asm__ volatile("wfi");             /* +1300 */

    svm_puts("XVM2: v0 alive after VM1 SYSTEM_OFF\n");
    /* The detector: vCPU1 shares pCPU1 with the VM that was just killed. */
    check(v1_ticks > ticks_before,
          "XVM2: v1 kept taking its own timer after VM1 SYSTEM_OFF\n");
    svm_done(0);
}
#else
/* VM1 vCPU0 (pCPU0): the only VM1 vCPU that runs on behalf of the VM. */
void SVM_ENTRY _start(void)
{
    t_start = now();
    busy_until(t_start + ms(20));
    svm_puts("XVM2: VM1 start\n");
    unsigned long r = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                               (unsigned long)xvm2_secondary_entry, 0UL);
    check(r == 0, "XVM2: VM1 CPU_ON(vCPU1) == SUCCESS\n");

    busy_until(t_start + ms(700));           /* RUNNING at the sweep */
    svm_puts("XVM2: VM1 powering off\n");
    (void)svm_hvc(PSCI_SYSTEM_OFF, 0UL);
    for (;;)
        __asm__ volatile("wfi");
}

/* VM1 vCPU1 (pCPU1, shares with VM0 vCPU1): busy until the sweep kicks it. */
static void secondary_main(void)
{
    busy_until(t_start + ms(700));
    for (;;)
        __asm__ volatile("wfi");
}
#endif
