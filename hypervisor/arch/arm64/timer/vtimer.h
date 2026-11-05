/* SPDX-License-Identifier: TBD */
#ifndef HV_VTIMER_H
#define HV_VTIMER_H

/* Configure the EL1 virtual timer for the guest: let EL1 read the physical
 * counter/timer without trapping (CNTHCTL_EL2=0b11) and use a zero virtual
 * offset (CNTVOFF_EL2=0). Must be called before vm_run(). */
void vtimer_init(void);

/* EL2 background timer (M11 C T4): program this pCPU's CNTHP to the
 * earliest virtual-timer deadline among its OFF-CPU BLOCKED vCPUs (or stop
 * it). Call on this pCPU after switching a vCPU in. */
void vtimer_bg_arm(void);

/* CNTHP PPI: wake every off-CPU blocked vCPU whose deadline passed, then
 * re-arm. Masked EL2 IRQ context. */
void vtimer_bg_expire(void);

#endif /* HV_VTIMER_H */
