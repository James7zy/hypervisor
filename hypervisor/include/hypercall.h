/* SPDX-License-Identifier: TBD */
#ifndef HV_INCLUDE_HYPERCALL_H
#define HV_INCLUDE_HYPERCALL_H

/*
 * HV-005: the guest <-> hypervisor call ABI, generic side. The call table
 * lives outside arch/ so it is host-testable and a second architecture only
 * swaps the trap entry around the same dispatch.
 *
 * Id space: SMCCC vendor-specific service range (DEN 0028 B),
 * base + (module << 8) + index. Ids are append-only: one that has shipped
 * keeps its value and its meaning.
 */

#include <types.h>

struct vm;

/* Return convention: x0 = HC_OK, or a negative errno. */
#define HC_OK        0
#define HC_EINVAL   -1
#define HC_EPERM    -2
#define HC_ENOSYS   -3
#define HC_ENODEV   -4

#define HVC_VENDOR_BASE 0x87000000U
#define HC_ID(module, index) \
    (HVC_VENDOR_BASE + ((u32)(module) << 8) + (u32)(index))

#define HC_MOD_GENERIC 0
#define HC_MOD_VM      1

#define HC_VERSION      HC_ID(HC_MOD_GENERIC, 0)
/* Not in the dispatch table: it never returns (the arch's HVC handler keeps
 * it and unwinds to the run loop). Its value predates this ABI. */
#define HC_GUEST_DONE   HC_ID(HC_MOD_GENERIC, 1)
#define HC_VM_QUERY     HC_ID(HC_MOD_VM, 0)

/* Byte width of the vmid field in the answers that carry one (bits[15:0]). */
#define HC_VMID_BITS 16

/*
 * Dispatch `func_id` for `caller`. The arch passes current_vcpu()->owner and
 * nothing from the guest's argument registers identifies the caller; `caller`
 * is therefore a precondition, not a parameter to validate (only an arch entry
 * calls this, and it always has a current vCPU).
 * Returns HC_OK or a negative errno; a word the caller should see in x1 comes
 * back through *ret (left untouched on failure).
 */
s64 hypercall_dispatch(struct vm *caller, u32 func_id, u64 a1, u64 a2,
                       u64 *ret);

#endif /* HV_INCLUDE_HYPERCALL_H */
