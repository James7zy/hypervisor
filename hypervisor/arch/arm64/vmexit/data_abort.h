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

struct abort_info {
    u32 ec;
    u32 fsc;
    const char *class_name;
    u64 ipa;
    bool far_valid;
    bool ipa_valid;
    bool wnr_valid;
    bool mmio_allowed;
};

struct abort_info abort_classify(const struct abort_snapshot *fault);

/* Only a successful dispatch changes GPRs or advances ELR. */
int mmio_handle_data_abort(struct arch_regs *regs, const struct abort_snapshot *fault);
void data_abort_report(const struct abort_snapshot *fault);

#endif /* HV_ARM64_DATA_ABORT_H */
