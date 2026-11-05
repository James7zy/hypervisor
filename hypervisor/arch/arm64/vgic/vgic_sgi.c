/* SPDX-License-Identifier: TBD */
/*
 * SGI / IPI virtualization (M3.5).
 *
 * A guest vCPU sends an IPI by writing the system register ICC_SGI1R_EL1. The
 * target vCPU may be running on another pCPU whose live ICH_LR_EL2 we cannot
 * write from the sender, so cross-core injection is two steps:
 *
 *   1. sender pCPU traps the ICC_SGI1R_EL1 write (ICH_HCR_EL2.TC=1), records
 *      the pending SGI in the *target* vCPU's shared bitmap, and issues a
 *      physical "kick" SGI (BOARD_KICK_SGI) to the target pCPU;
 *   2. the target pCPU takes that physical SGI to EL2, drains its own pending
 *      bitmap, injects each pending vINTID into its own LR, and deactivates
 *      the kick.
 *
 * The bitmap is the only cross-core-written state, so all access is under a
 * single SGI spinlock.
 */
#include <types.h>
#include <board.h>
#include <printk.h>
#include <vm.h>
#include <vm_config.h>
#include <sched.h>          /* sched_wake (M11 B2) */
#include <percpu.h>
#include <spinlock.h>
#include <arch/sysreg.h>
#include <vgic.h>
#include <gic_v3.h>
#include "vgic_sgi.h"

/* Guards every vcpu->arch.sgi_pending (cross-core RMW). */
static struct spinlock sgi_lock = SPINLOCK_INIT;

/* ICC_SGI1R_EL1 fields (Arm IHI0069). For our flat 2-vCPU topology the target
 * is the affinity-0 list (TargetList bitmap) at Aff1=Aff2=Aff3=0. */
#define SGI1R_INTID_SHIFT   24
#define SGI1R_INTID_MASK    0xFULL
#define SGI1R_TARGETLIST    0xFFFFULL          /* bits [15:0] */
#define SGI1R_IRM           (1ULL << 40)       /* 1 = broadcast to all but self */

/*
 * Trap path (sender pCPU): decode the guest's ICC_SGI1R_EL1 value and mark
 * each TARGET vCPU's own pending bitmap (M11 C T2: per vCPU, not per pCPU
 * — a pCPU may host several). Delivery by where the target is:
 *   - itself: drain now (we are its pCPU, it is cur_vcpu);
 *   - cur_vcpu of another pCPU: the kick's handler drains it;
 *   - queued RUNNABLE / BLOCKED: resume/load drains it after vgic_restore;
 *     sched_wake requeues a BLOCKED one (and kicks its pCPU).
 * The kick goes to the target's affinity pCPU unconditionally (as before);
 * if that pCPU's cur_vcpu is someone else its drain is a cheap no-op.
 */
void vgic_sgi_trap(u64 sgi1r)
{
    u32 vintid = (u32)((sgi1r >> SGI1R_INTID_SHIFT) & SGI1R_INTID_MASK);
    struct vcpu *sender = current_vcpu();
    struct vm   *m     = sender->owner;
    u32 self   = sender->vcpu_idx;
    u32 targets;

    if (sgi1r & SGI1R_IRM)
        targets = ((1U << VCPUS_PER_VM) - 1U) & ~(1U << self); /* all but self */
    else
        targets = (u32)(sgi1r & SGI1R_TARGETLIST);

    for (u32 idx = 0; idx < (u32)VCPUS_PER_VM; idx++) {
        if (!(targets & (1U << idx)))
            continue;

        struct vcpu *t = &m->vcpu[idx];
        u32 pcpu = m->config->vcpu_pcpu[idx];

        spin_lock(&sgi_lock);
        t->arch.sgi_pending |= (u16)(1U << vintid);
        spin_unlock(&sgi_lock);

        if (t == sender) {
            vgic_sgi_drain_vcpu(t);         /* self-IPI: no kick needed */
        } else {
            if (pcpu != current_vcpu_id())
                gic_kick_pcpu(pcpu);
            sched_wake(t);                  /* no-op unless BLOCKED */
        }
    }
}

/*
 * Inject `v`'s pending SGIs into its LRs, clearing only the bits that were
 * placed (vgic_place_sgis); leftovers wait for the EOI maintenance PPI or
 * the next resume. `v` must be this pCPU's cur_vcpu. Called from the
 * self-IPI trap, the kick and maintenance IRQ branches, and resume/load.
 */
void vgic_sgi_drain_vcpu(struct vcpu *v)
{
    spin_lock(&sgi_lock);
    u16 pend = v->arch.sgi_pending;
    if (pend)
        v->arch.sgi_pending = vgic_place_sgis(v, pend);
    spin_unlock(&sgi_lock);

    if (pend)
        sched_wake(v);   /* blocked cur_vcpu now has a pending vIRQ */
}
