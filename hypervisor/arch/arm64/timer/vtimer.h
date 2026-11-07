/* SPDX-License-Identifier: TBD */
#ifndef HV_VTIMER_H
#define HV_VTIMER_H

/* Configure the EL1 virtual timer for the guest: let EL1 read the physical
 * counter/timer without trapping (CNTHCTL_EL2=0b11) and use a zero virtual
 * offset (CNTVOFF_EL2=0). Must be called before vm_run(). */
void vtimer_init(void);

/* Program this pCPU's CNTHP to min(background vtimer earliest deadline,
 * preemption quantum deadline) — or stop it. THE ONLY writer of
 * CNTHP_CVAL/CTL: anything arming a deadline must go through here, or the
 * other deadline silently disappears. Call on this pCPU after switching a
 * vCPU in. */
void el2_timer_rearm(void);

/* CNTHP PPI: wake every off-CPU blocked vCPU whose deadline passed, then
 * re-arm. Masked EL2 IRQ context. */
void vtimer_bg_expire(void);

/* True when this pCPU's preemption quantum deadline has passed; consumes it
 * (el2_timer_rearm then programs the background deadline alone). */
bool vtimer_tick_expire(void);

#endif /* HV_VTIMER_H */
