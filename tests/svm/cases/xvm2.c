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
 *   +0    VM0 v1 (pCPU1): arms CNTV for +900 (the same deadline as v0 —
 *         while off-CPU only the EL2 background timer covers it), wfi.
 *         VM1 v1 (pCPU1): enables IRQs; v0 sends SGI 2, whose handler
 *         stays in WFE without EOI (Linux CPU-stop IPI behaviour).
 *   +700  VM1 v0: SYSTEM_OFF -> both VM1 vCPUs swept to OFF; VM1 v1 is
 *         RUNNING inside its IRQ on pCPU1, so it is kicked and unwinds.
 *         Its active priority must not leak into the survivor.
 *   +900  VM0 v0's timer fires (its own CNTV, or the EL2 background timer
 *         while it was off-CPU). It samples vCPU1's tick counter, waits on
 *         five 250 ms windows with its own timer. vCPU1 starts its 100 ms
 *         tick loop from the +900 wake, so the windows under test are
 *         entirely after the sweep.
 *   verdict: vCPU1 — sharing pCPU1 with the killed VM until now —
 *         must have been woken by its own timer at least once after the
 *         sweep. A survivor left stranded on pCPU1 (lost wake / bad
 *         cross-VM resume) shows up as a frozen counter.
 *
 * Run: make test-qemu-xvm2   (included in the `test` aggregate)
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
#if XVM_ID == 1
static volatile int secondary_ready;
static volatile int secondary_stopping;
#endif
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
    if (!cond) {
        svm_puts("XVM2: CHECK FAILED\n");
        svm_done(1);
    }
    svm_puts(what);
}

static unsigned long my_aff0(void)
{
    unsigned long m;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(m));
    return m & 0xFFUL;
}

/* VM0 takes vtimer PPI 27; VM1 takes a Linux-like CPU-stop SGI 2. */
void svm_irq_handler(void)
{
    unsigned long iar, intid;

    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
    intid = iar & 0xFFFFFFUL;
#if XVM_ID == 1
    if (intid == 2U) {
        /* Linux's stop IPI never EOIs: the vCPU is killed inside its IRQ.
         * WFE does not yield, so SYSTEM_OFF must kick this RUNNING vCPU. */
        secondary_stopping = 1;
        for (;;)
            __asm__ volatile("wfe");
    }
#endif
    if (intid == 27U) {
        /* Mask the virtual timer at once: the level PPI re-raises until the
         * guest deactivates, and this case takes the IRQ on BOTH vCPUs. */
        __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(0UL));
        if (my_aff0() == 0UL)
            timer0++;
        else
            v1_ticks++;
    }
    /* EOImode=0: EOIR already deactivates. No ICC_DIR write — it traps to
     * EL2 (unhandled) and its printk would interleave with guest lines. */
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"(iar));
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
    /* First deadline coincides with VM0 v0's: it is the one that has to
     * survive the SYSTEM_OFF at +700 — while off-CPU it is only covered by
     * the EL2 background timer (T4). */
    __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(t_start + ms(900)));
    __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
    __asm__ volatile("wfi");
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

    /* Detector (M11 C): vCPU1 shares pCPU1 with the VM that was just
     * killed. Once stranded it stays stranded, so require progress in FIVE
     * consecutive 250 ms windows rather than one sample. Each window spans
     * more than two secondary ticks, allowing for scheduling jitter. */
    unsigned long last = v1_ticks;
    int stalled = 0;
    for (int w = 0; w < 5; w++) {
        int before = timer0;
        unsigned long deadline = now() + ms(250);
        __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(deadline));
        __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
        while (timer0 == before)
            __asm__ volatile("wfi");
        check(now() >= deadline, "XVM2: sample window elapsed\n");
        if (v1_ticks == last)
            stalled++;
        last = v1_ticks;
    }

    svm_puts("XVM2: v0 alive after VM1 SYSTEM_OFF\n");
    check(stalled == 0,
          "XVM2: v1 kept taking its own timer after VM1 SYSTEM_OFF\n");
    svm_done(0);
}
#else
/* VM1 vCPU0 (pCPU0): the only VM1 vCPU that runs on behalf of the VM. */
void SVM_ENTRY _start(void)
{
    t_start = now();
    /* Staggered by 40 ms against VM0's +20 prints: the vuart TX path is
     * byte-serial and two guests printing at once garble both lines. */
    busy_until(t_start + ms(60));
    svm_puts("XVM2: VM1 start\n");
    unsigned long r = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                               (unsigned long)xvm2_secondary_entry, 0UL);
    check(r == 0, "XVM2: VM1 CPU_ON(vCPU1) == SUCCESS\n");

    while (!secondary_ready && now() < t_start + ms(600))
        ;
    check(secondary_ready, "XVM2: VM1 secondary ready\n");
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"((2UL << 24) | 2UL));
    while (!secondary_stopping && now() < t_start + ms(700))
        ;
    check(secondary_stopping, "XVM2: VM1 secondary stopped inside IRQ\n");
    busy_until(t_start + ms(700));           /* RUNNING at the sweep */
    svm_puts("XVM2: VM1 powering off\n");
    (void)svm_hvc(PSCI_SYSTEM_OFF, 0UL);
    for (;;)
        __asm__ volatile("wfi");
}

/* VM1 vCPU1 leaves an active priority on pCPU1 when SYSTEM_OFF kills it. */
static void secondary_main(void)
{
    svm_gic_el1_init();
    __asm__ volatile("msr daifclr, #2" ::: "memory");
    secondary_ready = 1;
    for (;;)
        __asm__ volatile("wfi");
}
#endif
