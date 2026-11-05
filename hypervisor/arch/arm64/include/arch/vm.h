/* SPDX-License-Identifier: TBD */
#ifndef HV_ARCH_VM_H
#define HV_ARCH_VM_H

/*
 * arm64 part of the vCPU (ADR-0015). The generic struct vcpu (<vm.h>) embeds
 * struct arch_regs first and struct vcpu_arch second, so the asm-visible
 * fields keep fixed offsets from the start of struct vcpu. The C layout and
 * the asm offset macros live together here (ADR-0003, as amended by
 * ADR-0015) and are pinned by tests/check_offsets*.c.
 */

#ifndef __ASSEMBLER__
#include <types.h>
#include <spinlock.h>

struct arch_regs {
    u64 x[31];      /* x0–x30   offset 0x000 */
    u64 sp_el1;     /*           offset 0x0F8 */
    u64 elr_el2;    /*           offset 0x100 */
    u64 spsr_el2;   /*           offset 0x108 */
};

struct vcpu_arch {
    u64 hcr_el2;             /* vcpu offset 0x110 */
    u64 vttbr_el2;           /* vcpu offset 0x118 */
    u64 ich_hcr_el2;         /* vcpu offset 0x120 */
    u64 ich_vmcr_el2;        /* vcpu offset 0x128 */
    u64 ich_lr[4];           /* vcpu offset 0x130 (LR0..LR3, 0x130..0x14F) */
    /* Fields below are NOT read by the exception-entry asm — append only. */
    /* M5 slice 3 fix: set by remote vgic_inject_spi() before kicking the
     * owning pCPU, test-and-cleared by vgic_reload_spi_lr() on that pCPU.
     * Distinguishes "this kick-SGI carries a freshly-shadowed PL011 SPI" from
     * "this kick-SGI is an ordinary cross-core IPI/park-check with nothing
     * new in ich_lr[1]" -- without it, vgic_reload_spi_lr() would blindly
     * replay a stale shadow (already consumed by the guest) back into the
     * live ICH_LR1_EL2 on every unrelated kick. Mirrors arch.sgi_pending's role
     * for the SGI/IPI case (vgic_sgi.c) but is per-vcpu, separate state --
     * does not interact with sgi_pending. */
    bool spi_shadow_pending;
    /* Serializes LR1 payload/pending/live completion; never reset at runtime. */
    struct spinlock spi_lock;
    /* Per-vCPU virtual-timer shadow (M11 B2, D4; appended tail — keeps the
     * generic struct vcpu tail's pinned offsets stable). CNTVOFF: per-vCPU
     * offset written at load/resume. cntv_ctl/cntv_cval: saved at put
     * (IMASKed) / block (saved, timer left LIVE — gate-1 R2). */
    u64 cntvoff;
    u64 cntv_ctl;
    u64 cntv_cval;
    /* EL1 bulk set (M11 B2 T4; Q9-derived, half-list-validated, bao/xvisor
     * diff in docs/debug/m11-b2-el1-halflist.md). The per-exception asm
     * frame covers GPRs/SP/ELR/SPSR; these are what two same-VM vCPUs of
     * one pCPU would otherwise clobber across a switch. */
    struct el1_state {
        u64 sctlr_el1;
        u64 ttbr0_el1;
        u64 ttbr1_el1;
        u64 tcr_el1;
        u64 mair_el1;
        u64 vbar_el1;
        u64 cpacr_el1;
        u64 tpidr_el1;
        u64 tpidrro_el0;
        u64 contextidr_el1;
        u64 esr_el1;
        u64 far_el1;
        /* M11 C T1b (gate-1 R1): SP_EL0 is Linux's `current`; ELR/SPSR_EL1
         * hold an in-flight EL1 exception; CNTKCTL is the guest's EL0
         * timer-access/event-stream control. */
        u64 sp_el0;
        u64 elr_el1;
        u64 spsr_el1;
        u64 tpidr_el0;
        u64 par_el1;
        u64 cntkctl_el1;
    } el1;
    /* Pending virtual SGIs (INTID 0..15 -> bits), per TARGET vCPU (M11 C
     * T2; was per pCPU, wrong once a pCPU hosts several vCPUs). Written
     * cross-core under vgic_sgi.c's sgi_lock; drained into LR2/LR3 by the
     * pCPU whose cur_vcpu this is. */
    u16 sgi_pending;
    /* Guest FP/SIMD context (M11 C T3, ADR-0020 eager): saved at put AND
     * block, restored at resume/load. Layout bound to fp.S by the pins in
     * check_offsets_target.c (q at 0, fpsr 512, fpcr 520, size 528). */
    struct fp_state {
        u64 q[64];
        u64 fpsr;
        u64 fpcr;
    } __attribute__((aligned(16))) fp;
    /* Physical Active state of the vtimer PPI 27 (M11 C, tester Critical-1).
     * HW-forwarded (LR0 HW=1): EL2 only priority-drops, the physical line
     * stays Active until THIS vCPU's guest deactivates through the LR link.
     * That state belongs to the vCPU, not the pCPU: saved at put/block,
     * forced to the incoming vCPU's value at resume/load. Left on the pCPU,
     * it silenced the next vCPU's timer until the owner came back — forever
     * if the owner was killed (SYSTEM_OFF of the other VM). */
    bool timer_ppi_active;
    /* Virtual running priorities belong to the vCPU, including an IRQ
     * handler suspended by a switch or killed before its EOI. */
    u64 ich_ap0r[4];
    u64 ich_ap1r[4];
};

/* Hypervisor callee-saved context, saved by vcpu_arch_run and restored by
 * hv_restore (vmexit_asm.S), one slot per pCPU. */
struct hv_ctx {
    u64 x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29;
    u64 lr;   /* offset 0x058 */
    u64 sp;   /* offset 0x060 */
};

extern void hv_restore(void) __attribute__((noreturn));   /* vmexit_asm.S */
#endif /* !__ASSEMBLER__ */

#ifdef __ASSEMBLER__
/* Offsets from the start of struct vcpu (arch_regs is its first member). */
#define VCPU_X0         0x000
#define VCPU_SP_EL1     0x0F8
#define VCPU_ELR        0x100
#define VCPU_SPSR       0x108
#define VCPU_HCR_EL2    0x110
#define VCPU_VTTBR_EL2  0x118
#define HV_LR           0x058
#define HV_SP           0x060
#define HV_CTX_SIZE     0x068
#endif /* __ASSEMBLER__ */

#endif /* HV_ARCH_VM_H */
