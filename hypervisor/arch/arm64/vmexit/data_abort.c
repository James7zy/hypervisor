/* SPDX-License-Identifier: TBD */
#include <types.h>
#include <printk.h>
#include <vm.h>
#include <mmio.h>
#include "data_abort.h"
void abort_snapshot_read(struct abort_snapshot *fault, u64 esr, u64 elr)
{
    fault->esr = esr;
    fault->elr = elr;
    asm volatile("mrs %0, far_el2" : "=r"(fault->far));
    asm volatile("mrs %0, hpfar_el2" : "=r"(fault->hpfar));
}

/*
 * Diagnostic for a data abort no MMIO handler claimed: which IPA, which fault.
 * The caller then parks this vCPU (HV-012 G5) — reporting and stopping are
 * separate so the vCPU state machine stays in vmexit.c.
 */
void data_abort_report(const struct abort_snapshot *fault)
{
    u32 iss = (u32)(fault->esr & 0x01FFFFFFU);

    /* VM-local vCPU index, not the pCPU: current_vcpu_id() is the physical
     * core (the repo's "[hv] ... cpu%u" convention); a fault on a shared pCPU
     * would otherwise be reported against the wrong vCPU. */
    printk("[hv] s2 fault: vCPU%u ipa=0x%lx fsc=0x%x ec=0x%x\n",
           (unsigned)current_vcpu()->vcpu_idx, ((fault->hpfar & 0xFFFFFFFFFFF0ULL) << 8) | (fault->far & 0xFFFULL),
           (unsigned)(iss & 0x3fU), (unsigned)((fault->esr >> 26) & 0x3FU));
}
