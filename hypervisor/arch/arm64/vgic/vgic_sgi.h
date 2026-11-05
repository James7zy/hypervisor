/* SPDX-License-Identifier: TBD */
#ifndef HV_VGIC_SGI_H
#define HV_VGIC_SGI_H

#include <types.h>

/* Sender pCPU: decode a guest ICC_SGI1R_EL1 write, mark target vCPU bitmaps, kick. */
void vgic_sgi_trap(u64 sgi1r);

struct vcpu;
/* Drain `v`'s pending SGI bitmap into its LRs; `v` is this pCPU's cur_vcpu. */
void vgic_sgi_drain_vcpu(struct vcpu *v);

#endif /* HV_VGIC_SGI_H */
