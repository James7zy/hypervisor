/* SPDX-License-Identifier: TBD */
#ifndef HV_VGIC_LR_H
#define HV_VGIC_LR_H

#include <types.h>

/* ICH_LR<n>_EL2 fields (GICv3, 64-bit list register). Pure: no struct vcpu,
 * so the host unit test can include it. */
#define ICH_LR_STATE_PENDING (1ULL << 62)   /* State[63:62] = 0b01 (Pending) */
#define ICH_LR_HW            (1ULL << 61)    /* 0 = software injection      */
#define ICH_LR_GROUP1        (1ULL << 60)    /* Group 1                     */
#define ICH_LR_PRIO_SHIFT    48              /* Priority[55:48]             */
#define ICH_LR_EOI           (1ULL << 41)    /* HW=0: maintenance IRQ on deactivate */
#define ICH_LR_PINTID_SHIFT  32              /* pINTID[44:32] (HW=1)        */
#define ICH_LR_VINTID_MASK   0xFFFFFFFFULL   /* vINTID[31:0]                */

/* Place the pending virtual SGIs `pend` (INTID 0..15 -> bits) into the two
 * SGI list-register images lr[0..1] (LR2/LR3); returns the bits that found
 * no slot. Rules in vgic_sgi_place.c. */
u16 vgic_sgi_place_lrs(u64 lr[2], u16 pend);

#endif /* HV_VGIC_LR_H */
