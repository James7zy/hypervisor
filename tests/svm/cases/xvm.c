/* SPDX-License-Identifier: TBD */
/*
 * SVM case "xvm" (M11 slice C T5): cross-VM isolation on a SHARED pCPU.
 * Profile svm_xvm: VM0 vcpu_pcpu = {0, 1}, VM1 = {2, 0} — VM0 vCPU0 and
 * VM1 vCPU1 share pCPU0. One source, built twice (-DXVM_ID=0/1, with
 * -mgeneral-regs-only so the FP canaries are trustworthy).
 *
 * Time axis: both VMs see the same CNTVCT (cntvoff = 0); each VM measures
 * from its own start (t_start), and the VMs boot within microseconds of
 * each other, so the 100+ ms phase margins below absorb the skew.
 *
 *   +0    VM0 v0: prints, CPU_ONs VM0 v1 (pCPU1), stamps EL1/FP/memory,
 *               arms CNTV for +300 ms, wfi alone -> BLOCKED.
 *   +100  VM1 v0 (pCPU2): CPU_ON VM1 v1 -> pCPU0, a FRESH load there.
 *         VM1 v1: EL1 and FP must read zero (no leak from VM0 v0), stamps
 *         its own, then busy-waits (no wfi) to +400 ms.
 *   +300  VM0 v0's deadline: only the EL2 background timer (CNTHP) can
 *         see it now — it wakes VM0 v0 into pCPU0's queue (T4).
 *   +400  VM1 v1 yields. Nothing but the bg timer can have queued VM0 v0
 *         yet (the SGIs come at +500 and would also wake it — so VM0 v0
 *         checks it resumed BEFORE +500), takes its own expired timer,
 *         and yields straight back.
 *   +500  VM0 v1 (pCPU1) sends SGI 5 and 6 to VM0 v0 while VM1 v1 is
 *         current on pCPU0: VM1 v1 must take none (T2, per-vCPU pending).
 *   +700  VM1 v1 yields; VM0 v0 resumes with both SGIs (LR2+LR3). 8
 *         rotation rounds; each side's EL1, FP and the same-IPA memory
 *         word must be its own.
 *   tail  VM1 v1 busy-loops forever; VM1 v0 SYSTEM_OFFs VM1 -> the kick
 *         unwinds VM1 v1 (T6) and pCPU0 runs VM0 v0 again: it prints last.
 *
 * Only VM0 v0 and VM1 v1 print (one pCPU, cooperative: no interleaving);
 * the off-pCPU0 vCPUs stay silent.
 *
 * Run: make test-qemu-xvm   (in the `test` aggregate)
 */
#include "../svm_lib.h"
#include "../svm_fp.h"

#ifndef XVM_ID
#error "build with -DXVM_ID=0 or 1"
#endif

#define PSCI_CPU_ON_64   0xC4000003UL
#define PSCI_SYSTEM_OFF  0x84000008UL
#define ROUNDS 8

#define STAMP(n)  ((XVM_ID ? 0xB1000000UL : 0xA1000000UL) + (n))
#define FP_SEED   (XVM_ID ? 0xB2A0000000000000UL : 0xA2A0000000000000UL)
#define FPCR_VAL  (XVM_ID ? 0x00C00000UL : 0x00400000UL)   /* RMode RZ / RP */
#define FPSR_VAL  (XVM_ID ? 0x2UL : 0x1UL)                 /* DZC / IOC */
#define WORD_VAL  (0xA000UL + XVM_ID)

static volatile unsigned long t_start;     /* this VM's time origin */
static volatile unsigned long shared_word; /* SAME IPA in both VMs */
static volatile int fired, sgi5, sgi6, sgi_other;
static volatile int v1_final;              /* VM1: v1 reached its tail */

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
    svm_puts(cond ? what : "XVM: CHECK FAILED\n");
}

void svm_irq_handler(void)
{
    unsigned long iar, intid;

    __asm__ volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
    intid = iar & 0xFFFFFFUL;
    if (intid == 27U) {
        __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(0UL));
        fired++;
    } else if (intid == 5U) {
        sgi5++;
    } else if (intid == 6U) {
        sgi6++;
    } else if (intid < 16U) {
        sgi_other++;
    }
    __asm__ volatile("msr ICC_EOIR1_EL1, %0" :: "r"(iar));
    __asm__ volatile("msr ICC_DIR_EL1, %0"   :: "r"(iar));
    __asm__ volatile("isb");
}

/* Inert full-value EL1 canaries (EL1h: SP_EL0/TPIDR_EL0 unused). */
static void el1_stamp(void)
{
    __asm__ volatile(
        "msr tpidr_el1, %0\n\tmsr tpidrro_el0, %1\n\tmsr contextidr_el1, %2\n\t"
        "msr sp_el0, %3\n\tmsr tpidr_el0, %4"
        :: "r"(STAMP(0)), "r"(STAMP(1)), "r"(STAMP(2)), "r"(STAMP(3)),
           "r"(STAMP(4)));
}

static int el1_intact(void)
{
    unsigned long a, b, c, d, e;
    __asm__ volatile(
        "mrs %0, tpidr_el1\n\tmrs %1, tpidrro_el0\n\tmrs %2, contextidr_el1\n\t"
        "mrs %3, sp_el0\n\tmrs %4, tpidr_el0"
        : "=r"(a), "=r"(b), "=r"(c), "=r"(d), "=r"(e));
    return a == STAMP(0) && b == STAMP(1) && c == STAMP(2) &&
           d == STAMP(3) && e == STAMP(4);
}

static void enable_fp(void)
{
    __asm__ volatile("msr cpacr_el1, %0\n\tisb" :: "r"(3UL << 20));  /* FPEN=11 */
}

static void stamp_all(void)
{
    el1_stamp();
    fp_stamp(FP_SEED, FPCR_VAL, FPSR_VAL);
    shared_word = WORD_VAL;
}

static int all_intact(void)
{
    return el1_intact() && fp_check(FP_SEED, FPCR_VAL, FPSR_VAL) == 0UL &&
           shared_word == WORD_VAL;
}

static void secondary_main(void) __attribute__((used, noreturn));

extern char xvm_secondary_entry[];
__asm__(
    ".global xvm_secondary_entry\n"
    "xvm_secondary_entry:\n"
    "   msr daifset, #0xf\n"
    "   ldr x0, =v1_stack + 0x4000\n"
    "   mov sp, x0\n"
    "   b secondary_main\n"
);

#if XVM_ID == 0
/* VM0 vCPU1 (pCPU1, silent): the cross-VM SGI source. */
static void secondary_main(void)
{
    busy_until(t_start + ms(500));
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"((5UL << 24) | 1UL));
    __asm__ volatile("msr ICC_SGI1R_EL1, %0" :: "r"((6UL << 24) | 1UL));
    __asm__ volatile("isb");
    for (;;)
        __asm__ volatile("wfi");             /* alone on pCPU1: sleeps */
}

void SVM_ENTRY _start(void)
{
    t_start = now();
    busy_until(t_start + ms(20));            /* past the boot banners */
    svm_puts("XVM0: start\n");
    unsigned long r = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                               (unsigned long)xvm_secondary_entry, 0UL);
    check(r == 0, "XVM0: CPU_ON(vCPU1) == SUCCESS\n");

    /* Leak sources for VM1 v1's fresh-load check: VBAR (gic init), CPACR,
     * EL1 stamps, FP file. Also this side's isolation canaries. */
    svm_gic_el1_init();
    enable_fp();
    stamp_all();

    __asm__ volatile("msr CNTV_CVAL_EL0, %0" :: "r"(t_start + ms(300)));
    __asm__ volatile("msr CNTV_CTL_EL0, %0\n\tisb" :: "r"(1UL));
    __asm__ volatile("msr daifclr, #2");
    while (!fired)
        __asm__ volatile("wfi");             /* alone -> BLOCKED */
    /* Printing is deferred: VM1 v1 must be current again by +500. */
    unsigned long t_wake = now();
    unsigned long ctl;
    __asm__ volatile("mrs %0, CNTV_CTL_EL0" : "=r"(ctl));
    while (!(sgi5 && sgi6))
        __asm__ volatile("wfi");             /* first one yields to VM1 v1 */
    check(fired == 1 && ctl == 0UL && t_wake >= t_start + ms(300) &&
          t_wake < t_start + ms(500),
          "XVM0: own timer woke it via the bg timer, before the SGIs\n");
    check(sgi5 == 1 && sgi6 == 1 && sgi_other == 0,
          "XVM0: SGI 5 and 6 received\n");

    /* Round 8 does not return until VM1 v1 stops running: it busy-loops
     * forever after its own round 8, so only the SYSTEM_OFF kick unwind
     * (T6) gives pCPU0 back. */
    for (int i = 0; i < ROUNDS; i++)
        __asm__ volatile("wfi");
    check(all_intact(), "XVM0: isolation intact (EL1/FP/memory)\n");
    svm_puts("XVM0: ran after VM1 SYSTEM_OFF\n");
    svm_done(0);
}
#else
/* VM1 vCPU1 (pCPU0, shared with VM0 v0). */
static void secondary_main(void)
{
    {
        unsigned long a, b, c, d, e, f, g, h, i, j, k, l;
        __asm__ volatile(
            "mrs %0, tpidr_el1\n\tmrs %1, tpidrro_el0\n\tmrs %2, contextidr_el1\n\t"
            "mrs %3, sp_el0\n\tmrs %4, tpidr_el0\n\tmrs %5, vbar_el1\n\t"
            "mrs %6, cpacr_el1\n\tmrs %7, sctlr_el1\n\tmrs %8, ttbr0_el1\n\t"
            "mrs %9, ttbr1_el1\n\tmrs %10, tcr_el1\n\tmrs %11, mair_el1"
            : "=r"(a), "=r"(b), "=r"(c), "=r"(d), "=r"(e), "=r"(f),
              "=r"(g), "=r"(h), "=r"(i), "=r"(j), "=r"(k), "=r"(l));
        check((a | b | c | d | e | f | g | h | i | j | k | l) == 0UL,
              "XVM1: fresh-load EL1 zero\n");
    }
    enable_fp();
    check(fp_check_zero() == 0UL, "XVM1: fresh-load FP zero\n");

    svm_gic_el1_init();
    __asm__ volatile("msr daifclr, #2");
    stamp_all();

    busy_until(t_start + ms(400));           /* VM0's deadline passes now */
    __asm__ volatile("wfi");                 /* yield: VM0 v0 queued only by CNTHP */
    busy_until(t_start + ms(700));           /* SGIs for VM0 v0 land now */
    check(sgi5 + sgi6 + sgi_other == 0, "XVM1: no SGI leaked from VM0\n");

    for (int i = 0; i < ROUNDS; i++)
        __asm__ volatile("wfi");             /* rotate with VM0 v0 */
    check(all_intact(), "XVM1: isolation intact (EL1/FP/memory)\n");

    v1_final = 1;
    __asm__ volatile("dsb ish" ::: "memory");
    for (;;)
        ;                                    /* RUNNING until SYSTEM_OFF */
}

/* VM1 vCPU0 (pCPU2, silent): brings v1 up on pCPU0, then powers VM1 off
 * while v1 is running there. */
void SVM_ENTRY _start(void)
{
    t_start = now();
    busy_until(t_start + ms(100));
    unsigned long r = svm_hvc4(PSCI_CPU_ON_64, 1UL,
                               (unsigned long)xvm_secondary_entry, 0UL);
    if (r != 0)
        svm_puts("XVM1: CPU_ON FAILED\n");
    while (!v1_final)
        ;
    busy_until(now() + ms(100));
    svm_hvc(PSCI_SYSTEM_OFF, 0UL);
    for (;;)
        __asm__ volatile("wfi");
}
#endif
