/* SPDX-License-Identifier: TBD */
#ifndef ARCH_STAGE2_H
#define ARCH_STAGE2_H

#include <types.h>
#include <vm.h>

/* Build this VM's initial Stage-2 mappings from the board config. Returns 0,
 * or a negative code when the table pool is exhausted or the G6 equivalence
 * self-check fails (the VM then never runs). */
int  stage2_init(struct vm *m);
void stage2_activate(const struct vcpu *vcpu);
/* Invalidate every stage-2 entry of every VMID on this PE (VM-level, that is:
 * no range tracking). Call after any map/unmap/protect. */
void stage2_flush_vm(void);

#endif /* ARCH_STAGE2_H */
