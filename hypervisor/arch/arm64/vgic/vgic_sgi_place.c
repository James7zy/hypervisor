/* SPDX-License-Identifier: TBD */
/*
 * Virtual-SGI slot placement over LR2/LR3 (M11 C T2). Pure — no sysregs —
 * so tests/host covers it. Two slots because under Linux SMP several IPI
 * kinds (RESCHEDULE, CALL_FUNC, ...) are routinely pending at once; the old
 * single LR2 overwrote one with the next. Per pending INTID:
 *   - already in a slot (pending and/or active): OR in Pending — SGIs
 *     coalesce, and one vINTID must never sit in two LRs;
 *   - else a free slot (State == 0): inject;
 *   - else: leave the bit for the caller.
 * While leftovers exist the occupied slots carry EOI: the guest's
 * deactivate of either raises the maintenance PPI, whose drain refills it.
 * Not NPIE: NPIE stays asserted while both slots are active-only with
 * nothing injectable — a level storm that starves the guest. Free slots are
 * rewritten as 0, which also drops a stale EOI condition.
 */
#include "vgic_lr.h"

static u64 sgi_lr_encode(u32 intid)
{
    return ICH_LR_STATE_PENDING | ICH_LR_GROUP1 |
           ((u64)0xA0 << ICH_LR_PRIO_SHIFT) |
           ((u64)intid & ICH_LR_VINTID_MASK);
}

static bool lr_free(u64 lr)
{
    return (lr >> 62) == 0ULL;
}

u16 vgic_sgi_place_lrs(u64 lr[2], u16 pend)
{
    for (u32 intid = 0; intid < 16U; intid++) {
        u16 bit = (u16)(1U << intid);
        if (!(pend & bit))
            continue;
        for (int i = 0; i < 2; i++) {
            if (!lr_free(lr[i]) && (lr[i] & ICH_LR_VINTID_MASK) == intid) {
                lr[i] |= ICH_LR_STATE_PENDING;
                pend &= (u16)~bit;
                break;
            }
        }
        if (!(pend & bit))
            continue;
        for (int i = 0; i < 2; i++) {
            if (lr_free(lr[i])) {
                lr[i] = sgi_lr_encode(intid);
                pend &= (u16)~bit;
                break;
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        if (lr_free(lr[i]))
            lr[i] = 0;
        else if (pend)
            lr[i] |= ICH_LR_EOI;
        else
            lr[i] &= ~ICH_LR_EOI;
    }
    return pend;
}
