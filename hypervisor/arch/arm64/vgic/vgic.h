/* SPDX-License-Identifier: TBD */
#ifndef HV_VGIC_H
#define HV_VGIC_H

#include <vm.h>   /* struct vcpu */

/* ICC_SRE_EL2: enable the EL2 system-register interface and permit EL1 to
 * use ICC_SRE_EL1. */
#define ICC_SRE_EL2_SRE     (1ULL << 0)
#define ICC_SRE_EL2_ENABLE  (1ULL << 3)

/* ICH_HCR_EL2: virtual CPU interface enable + SGI trap control. */
#define ICH_HCR_EL2_EN      (1ULL << 0)
#define ICH_HCR_EL2_TC      (1ULL << 10)   /* trap guest ICC_SGI*R_EL1 to EL2 */

#include "vgic_lr.h"   /* ICH_LR_* fields */

/* Pre-entry initialization only; never resets the BSS-zeroed SPI lock. */
void vgic_init(struct vcpu *vcpu);

/* Inject a pending, Group-1, software (HW=0) virtual interrupt via ICH_LR0.
 * Writes the live register so the vIRQ is presented on the next eret to EL1. */
void vgic_inject_sw(struct vcpu *vcpu, u32 vintid, u8 prio);

/* Inject a pending, Group-1, hardware-forwarded (HW=1) virtual interrupt via
 * ICH_LR0: the physical INTID is linked in pINTID so the guest's deactivate of
 * the virtual IRQ releases the physical one. See docs/adr/0001-*. */
void vgic_inject_hw(struct vcpu *vcpu, u32 vintid, u32 pintid, u8 prio);

/* Initiate delivery of the supported PL011 SPI: fixed LR1, software HW=0,
 * Group1, priority 0xA0. Requires initialized target/owner/config, a valid
 * vcpu_idx, stable static 1:1 pinning and EL2 trap/IRQ context with IRQs masked.
 *
 * Writes live LR1 only when target == current_vcpu(); otherwise internally
 * publishes, orders and kicks the target's affinity pCPU
 * (owner->config->vcpu_pcpu[vcpu_idx], via gic_kick_pcpu) for reload.
 * Payload, pending and any live write/consumption share the target's spi_lock.
 * A remote target must be online and able to handle the kick. No startup,
 * offline or lifecycle guarantee, scheduling support, or IRQ queue is added.
 * Return means delivery initiated, not guest acknowledgement; notifications
 * may coalesce (no IRQ-per-character guarantee). */
void vgic_inject_spi(struct vcpu *target, u32 intid);
/* True if any live ICH_LRx_EL2 on THIS pCPU holds pending state (M11 B2;
 * live-only by design — see vgic.c). */
bool vgic_has_pending(struct vcpu *vcpu);
/* Boot-time ICH_VTR probe: BUG-halts if the GIC implements < 4 LRs (M11 B2). */
void vgic_probe(void);

/* Cross-core companion to vgic_inject_spi: reload ICH_LR1_EL2 on the CALLING
 * pCPU from its own current vCPU's shadow ich_lr[1]. Called unconditionally by
 * the kicked target pCPU (el2_irq_handler's kick-SGI branch) on EVERY kick,
 * including ones that are ordinary SGI/IPI traffic -- it test-and-clears
 * vcpu->arch.spi_shadow_pending and only actually reloads the live register when
 * that flag was set by remote vgic_inject_spi since the last consumption.
 * This prevents replaying a stale, already-consumed shadow LR1 on an unrelated
 * later kick. Payload, live write and consumption share spi_lock; caller must
 * pass its current vCPU in masked EL2 IRQ context. */
void vgic_reload_spi_lr(struct vcpu *vcpu);

/* Place pending virtual SGIs (bitmap `pend`) into the SGI list registers
 * LR2/LR3 of `vcpu`, which must be THIS pCPU's cur_vcpu (its state is in
 * the live LRs). Returns the bits that found no LR. Software (HW=0)
 * Group-1. Used by vgic_sgi.c's drain under sgi_lock. */
u16 vgic_place_sgis(struct vcpu *vcpu, u16 pend);

/* save has no callers; restore is initial-entry-only under static pinning.
 * Neither is runtime scheduler-safe against remote SPI publication. */
void vgic_save(struct vcpu *vcpu);
void vgic_restore(struct vcpu *vcpu);

#endif /* HV_VGIC_H */
