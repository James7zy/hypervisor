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

/* One record binds fault identity, syndrome and address validity. */
void data_abort_report(const struct abort_snapshot *fault)
{
    struct abort_info info = abort_classify(fault);
    struct vcpu *v = current_vcpu();

    printk("[hv] s2 fault: vCPU%u ipa=0x%lx fsc=0x%x ec=0x%x "
           "class=%s vm=%u vcpu=%u pcpu=%u ipa_valid=%u far_valid=%u "
           "far=0x%lx elr=0x%lx esr=0x%lx wnr_valid=%u wnr=%u\n",
           (unsigned)v->vcpu_idx, info.ipa_valid ? info.ipa : 0,
           info.fsc, info.ec, info.class_name, (unsigned)v->owner->id,
           (unsigned)v->vcpu_idx, (unsigned)current_vcpu_id(),
           (unsigned)info.ipa_valid, (unsigned)info.far_valid, fault->far,
           fault->elr, fault->esr, (unsigned)info.wnr_valid,
           (unsigned)((fault->esr >> 6) & 1));
}
