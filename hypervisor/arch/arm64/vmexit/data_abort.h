/* SPDX-License-Identifier: TBD */
#ifndef HV_ARM64_DATA_ABORT_H
#define HV_ARM64_DATA_ABORT_H

#include <types.h>
#include <arch/vm.h>

struct abort_snapshot {
    u64 esr;
    u64 far;
    u64 hpfar;
    u64 elr;
};

void abort_snapshot_read(struct abort_snapshot *fault, u64 esr, u64 elr);

/*
 * Stage-2 data-abort entry point (EC = 0x24). Decodes ESR_EL2 ISS + HPFAR/FAR
 * into a struct mmio_access, dispatches it on the generic MMIO bus (<mmio.h>),
 * plumbs the transfer register, and on success advances regs->elr_el2 past
 * the faulting instruction.
 * Returns 0 if handled (caller erets back), non-zero if unhandled.
 */
int mmio_handle_data_abort(struct arch_regs *regs, const struct abort_snapshot *fault);

/* Log an unhandled data abort as `[hv] s2 fault: vCPU<x> ipa=… fsc=… ec=…`
 * (ipa/fsc/ec of the faulting access). The caller parks the vCPU. */
void data_abort_report(const struct abort_snapshot *fault);

#endif /* HV_ARM64_DATA_ABORT_H */
