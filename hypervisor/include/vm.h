/* SPDX-License-Identifier: TBD */
#ifndef HV_VM_H
#define HV_VM_H

#ifndef __ASSEMBLER__
#include <types.h>
#include <spinlock.h>
#include <vcpu_state.h>
#include <percpu.h>   /* NR_CPUS (percpu.h forward-declares struct vcpu) */
#include <arch/vm.h>  /* struct arch_regs, struct vcpu_arch */

/* Compile-time VM count. 1 until M5 slice 3; the Makefile overrides it
 * per guest profile (-DCONFIG_NR_VMS=N). */
#ifndef CONFIG_NR_VMS
#define CONFIG_NR_VMS 1
#endif
#define NR_VMS       CONFIG_NR_VMS
#define VCPUS_PER_VM 2   /* static 2-vCPU VMs; affinity must fit NR_CPUS
                            (validated per entry at boot in vm_init — the
                            compile-time NR_VMS*VCPUS_PER_VM assert was
                            replaced by that check, M11 slice A D6) */

struct vm;

struct vcpu {
    struct arch_regs regs;   /* MUST be first: guest GPR frame (asm offset 0) */
    struct vcpu_arch arch;   /* arch-private vCPU state; asm-visible fields are
                                laid out in <arch/vm.h> */
    /* Generic fields below are NOT read by the exception-entry asm. */
    struct vm *owner;        /* back-pointer: trap handlers navigate via
                                current_vcpu()->owner instead of globals */
    u32        vcpu_idx;     /* VM-local vCPU index (the guest's CPU number) */
    enum vcpu_state state;   /* per-vCPU run state (M11 slice A); appended
                                after the asm-visible prefix — existing
                                VCPU_* offsets unchanged (pinned in
                                tests/check_offsets_target.c) */
    bool       launched;     /* has run at least once since its latest CPU_ON
                                authorization (M11 B1): false → next entry is
                                a FRESH boot (vcpu_arch_load: vGIC blank +
                                EL1 neutralize); true → resume (vGIC shadow
                                restore only) */
};

struct vm_config;

struct vm {
    struct vcpu            vcpu[VCPUS_PER_VM];
    u32                    id;        /* index into vm[]; keys the per-VM
                                         file-static state in arch code
                                         and in dm/vuart.c */
    const struct vm_config *config;
    bool                   paused;    /* HV-004: suspend flag, a scheduling
                                         predicate -- see vm_is_paused() */
};

extern struct vm vm[NR_VMS];

void vm_init(void);
void vm_run(void);

/*
 * The vCPU statically affined to pCPU `pcpu` (vm_config.vcpu_pcpu[]), or NULL
 * if none. With 1:1 tables this is the pCPU's only vCPU. Since B1 the run
 * loops pick through the per-pCPU RR queues instead; this is now the boot
 * identity check + cur_vcpu seed. (Shared-affinity identity per vCPU is
 * the queue's job, not a "lowest" pick anymore.)
 */
struct vcpu *vcpu_for_pcpu(u32 pcpu);

/*
 * Derived per-VM state: a VM is "on" while any of its vCPUs is not OFF. There
 * is no VM-level state machine yet, so the EL2 shell's vm_list and
 * HC_VM_QUERY both read this -- one implementation, one answer.
 */
bool vm_any_vcpu_on(const struct vm *v);
u32  vm_online_vcpus(const struct vm *v);
/* Service VM = VM0: static topology (ADR-0014), not a config field (E3). */
bool vm_is_service(const struct vm *v);

/*
 * Suspend state (HV-004 F1: two states only, so no enum until VM create/
 * destroy brings the rest). `paused` is a SCHEDULING predicate, not "the guest
 * is stopped": sched_loop drops a paused VM's vCPU at pick and sched_vm_pause
 * kicks the resident off its pCPU, so the effect is asynchronous -- a hypercall
 * can return before the target has left the hardware.
 *
 * Defined here rather than in vm.c because vm.c owns the vm[] array that the
 * host scheduler test cannot link; the flag is one bool and these are all of
 * its transitions.
 * Returns 0 on a real transition, VM_EALREADY if already in that state (the
 * hypercall layer maps it to HC_EBUSY).
 */
#define VM_EALREADY (-1)

static inline bool vm_is_paused(const struct vm *v)
{
    return __atomic_load_n(&v->paused, __ATOMIC_ACQUIRE);
}

static inline int vm_pause(struct vm *v)
{
    if (__atomic_exchange_n(&v->paused, true, __ATOMIC_ACQ_REL))
        return VM_EALREADY;
    return 0;
}

static inline int vm_resume(struct vm *v)
{
    if (!__atomic_exchange_n(&v->paused, false, __ATOMIC_ACQ_REL))
        return VM_EALREADY;
    return 0;
}

/*
 * VM/vCPU hooks every arch implements (ADR-0015); no weak defaults.
 */
/* Per-VM arch setup (second-stage translation tables and their identifier).
 * Non-zero means the VM cannot run: vm_init reports it and leaves every vCPU
 * OFF (never halts the pCPU). */
int vm_arch_init(struct vm *m);
/* Register the arch's emulated platform devices (e.g. the interrupt
 * controller) on the MMIO bus, once for all VMs. */
void vm_arch_devices_init(void);
/* Put a VM's boot vCPU in its architectural reset/boot state for `cfg`
 * (boot-protocol registers, virtualization controls, virtual interrupt
 * interface). */
void vcpu_arch_reset(struct vcpu *v, const struct vm_config *cfg);
/* Load `v` onto this pCPU's virtualization hardware (CPU identity,
 * second-stage tables, virtual interrupt state) and neutralize this pCPU's
 * live EL1 CPU state for a fresh boot: SCTLR_EL1=0 (M=0 makes residual
 * TTBR/TCR/MAIR/etc. inert until the guest reprograms them) plus a stage-1
 * TLB + I-cache flush. Not a full warm reset: FP/SIMD, PMU and vtimer
 * residuals survive (known limitation, revisit on first non-Linux guest or
 * real hardware). */
void vcpu_arch_load(struct vcpu *v);
/* Raise virtual interrupt `irq` for vCPU `v`; callable from any pCPU
 * (kicking v's pCPU if remote). Only shared peripheral interrupts are
 * supported (the vuart's) -- the one caller today. */
void vcpu_arch_inject_irq(struct vcpu *v, u32 irq);
/* Enter the guest on this pCPU; returns on an exit that needs the caller. */
void vcpu_arch_run(struct vcpu *v);
/* Save `v`'s arch state as it leaves this pCPU in a real switch (M11 B1:
 * vGIC shadow only — the EL1 bulk set is slice B2; called by the run loops
 * ONLY on switch-away, never on the yield/same-vCPU re-entry path). */
void vcpu_arch_put(struct vcpu *v);
/* Re-enter an ALREADY-launched vCPU on this pCPU: vGIC shadow restore only —
 * NO vgic_init blanking, NO EL1 neutralize (that would wipe a live guest). */
void vcpu_arch_resume(struct vcpu *v);
/* Block-site save: leaves the CPU without a switch (M11 B2 R1) — same
 * shadows as put; the physical timer stays armed (block's wake source). */
void vcpu_arch_block(struct vcpu *v);
#endif /* !__ASSEMBLER__ */

#ifdef __ASSEMBLER__
#include <arch/vm.h>   /* asm offset macros into struct vcpu / struct hv_ctx */
#endif

#endif /* HV_VM_H */
