/* SPDX-License-Identifier: TBD */
/*
 * Virtual timer setup for the guest.
 *
 * CNTHCTL_EL2.EL1PCTEN (bit 0) and EL1PCEN (bit 1) let the guest read
 * CNTPCT_EL0 and access the physical timer registers without trapping to EL2
 * as EC=0x18. CNTVOFF_EL2=0 gives the guest the host timebase (single VM, no
 * migration — see spec §5.3).
 */
#include <types.h>
#include <printk.h>
#include <arch/sysreg.h>
#include <vtimer.h>
#include <vm.h>
#include <vm_config.h>
#include <percpu.h>
#include <sched.h>

void vtimer_init(void)
{
    /* CNTVOFF_EL2 is per-vCPU since M11 B2 (vcpu_arch load/resume program
     * it from the shadow); only the EL1 access trap control is global. */
    SYSREG_WRITE(CNTHCTL_EL2, 0x3ULL);   /* EL1PCTEN | EL1PCEN */
    asm volatile("isb");

    printk("[hv] vtimer: CNTHCTL_EL2=0x3 (CNTVOFF per-vCPU, B2)\n");
}

/*
 * EL2 background timer (M11 C T4, gate-1 R4). A vCPU that blocks leaves its
 * CNTV live (its own wake source), but once this pCPU switches another vCPU
 * in, the live CNTV is the newcomer's: the sleeper's deadline would never
 * fire. One CNTHP per pCPU stands in for all of them, armed at the EARLIEST
 * deadline (QEMU virt/cortex-a72 is v8.0 — no CNTHV; slice D's preemption
 * tick will share this timer as min(tick, earliest)).
 *
 * Physical deadline: CNTVCT = CNTPCT - CNTVOFF, so the virtual compare
 * cntv_cval fires at CNTPCT >= cntv_cval + cntvoff. Only timers with
 * ENABLE=1 and IMASK=0 can wake the guest. The shadows read here are
 * written only on this pCPU (block/put run on the affinity pCPU), so the
 * scan needs no lock; a state racing BLOCKED->RUNNABLE remotely costs at
 * most one spurious (no-op) wake.
 */
#define CNTV_CTL_ENABLE (1ULL << 0)
#define CNTV_CTL_IMASK  (1ULL << 1)

static bool bg_deadline(const struct vcpu *v, u64 *dl)
{
    u32 me = current_vcpu_id();

    if (v == current_vcpu() ||
        v->owner->config->vcpu_pcpu[v->vcpu_idx] != me ||
        __atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VCPU_BLOCKED)
        return false;
    if ((v->arch.cntv_ctl & (CNTV_CTL_ENABLE | CNTV_CTL_IMASK)) != CNTV_CTL_ENABLE)
        return false;
    *dl = v->arch.cntv_cval + v->arch.cntvoff;
    return true;
}

void vtimer_bg_arm(void)
{
    u64 earliest = ~0ULL;
    bool any = false;

    for (u32 i = 0; i < (u32)NR_VMS; i++) {
        for (u32 j = 0; j < (u32)VCPUS_PER_VM; j++) {
            u64 dl;
            if (bg_deadline(&vm[i].vcpu[j], &dl)) {
                any = true;
                if (dl < earliest)
                    earliest = dl;
            }
        }
    }
    if (any) {
        SYSREG_WRITE(CNTHP_CVAL_EL2, earliest);
        SYSREG_WRITE(CNTHP_CTL_EL2, 1ULL);      /* ENABLE, unmasked */
    } else {
        SYSREG_WRITE(CNTHP_CTL_EL2, 0ULL);
    }
    asm volatile("isb");
}

void vtimer_bg_expire(void)
{
    u64 now = SYSREG_READ(CNTPCT_EL0);

    for (u32 i = 0; i < (u32)NR_VMS; i++) {
        for (u32 j = 0; j < (u32)VCPUS_PER_VM; j++) {
            struct vcpu *v = &vm[i].vcpu[j];
            u64 dl;
            if (bg_deadline(v, &dl) && dl <= now)
                sched_wake(v);   /* resume restores its expired CNTV: PPI 27 re-raises */
        }
    }
    vtimer_bg_arm();   /* woken ones are no longer BLOCKED: next earliest or off */
}
