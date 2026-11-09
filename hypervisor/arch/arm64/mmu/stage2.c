/* SPDX-License-Identifier: TBD */
#include <types.h>
#include <vm.h>
#include <board.h>
#include <printk.h>
#include <vm_config.h>   /* struct vm_config (vmid / ram_pa) */
#include <s2_table.h>
#include "el2_mmu.h"     /* MAIR_EL2 slot layout (EL2's own stage 1) */
#include "stage2.h"
#include "stage2_build.h"

/*
 * VTCR_EL2: T0SZ=25 (39-bit IPA), SL0=1 (L1 start), IRGN0/ORGN0=1 (WB RA-WA),
 * SH0=3 (Inner Shareable), TG0=0 (4KB granule), PS=2 (40-bit PA).
 * Bit 31 is RES1 per ARM DDI0487 VTCR_EL2 definition.
 */
#define VTCR_RES1   (1ULL  << 31)
#define VTCR_T0SZ   (25ULL << 0)
#define VTCR_SL0    (1ULL  << 6)
#define VTCR_IRGN0  (1ULL  << 8)
#define VTCR_ORGN0  (1ULL  << 10)
#define VTCR_SH0    (3ULL  << 12)
#define VTCR_TG0    (0ULL  << 14)
#define VTCR_PS     (2ULL  << 16)
#define VTCR_EL2_VALUE \
    (VTCR_RES1 | VTCR_T0SZ | VTCR_SL0 | VTCR_IRGN0 | VTCR_ORGN0 | VTCR_SH0 | VTCR_TG0 | VTCR_PS)

/* 4 KB table pages (HV-011 G1) and one L1 root per VM. VTTBR_EL2[11:0] are
 * reserved, so both must be 4 KB-aligned. */
static u64 s2_pool_mem[S2_POOL_PAGES * 512] __attribute__((aligned(4096)));
static struct s2_pool s2_pool;
static bool s2_pool_ready;
static u64 s2_root[NR_VMS][512] __attribute__((aligned(4096)));

int stage2_init(struct vm *m)
{
    u64 *root = s2_root[m->id];
    int rc;

    if (!s2_pool_ready) {
        rc = s2_pool_init(&s2_pool, s2_pool_mem, S2_POOL_PAGES);
        if (rc != 0) {
            printk("[hv] stage2: table pool init failed (%d)\n", (int)rc);
            return rc;
        }
        s2_pool_ready = true;
    }

    /*
     * MAIR_EL2 slots 0/1 = Device-nGnRE / Normal WB: the same two attributes
     * the descriptors below encode directly (D8.2.6 — a stage-2 walk never
     * reads MAIR_EL2). Written explicitly instead of trusting reset values,
     * in el2_mmu.c's slot order because EL2's own stage 1 does index it.
     */
    __asm__ volatile("msr mair_el2, %0\nisb" :: "r"((u64)MAIR_EL2_VALUE)
                     : "memory");

    rc = stage2_build(root, &s2_pool, m->config);

    if (rc != 0) {
        /* Pool exhausted / a mapping changed shape: this VM cannot run.
         * Report it; the caller keeps the VM's vCPUs OFF (no pCPU halt). */
        printk("[hv] stage2: VM%u table build failed (%d)\n",
               (unsigned)m->id, (int)rc);
        return rc;
    }

    stage2_flush_vm();

    /* Every vCPU of the VM shares the same Stage-2 table / VMID. */
    u64 vttbr = ((u64)m->config->vmid << 48) | (u64)(uintptr_t)root;
    for (u32 i = 0; i < VCPUS_PER_VM; i++)
        m->vcpu[i].arch.vttbr_el2 = vttbr;
    return 0;
}

void stage2_activate(const struct vcpu *vcpu)
{
    /* dsb register 
     * Part D → Chapter D8 → D8.2 Translation process → D8.2.6 Translation table walk properties → 
     * “Ordering of memory accesses from translation table walks”
     * */
    asm volatile(
        "dsb ish\n"
        "msr vtcr_el2,  %0\n"
        "msr vttbr_el2, %1\n"
        "isb\n"
        :
        : "r"((u64)VTCR_EL2_VALUE), "r"(vcpu->arch.vttbr_el2)
        : "memory"
    );
}

void stage2_flush_vm(void)
{
    /*
     * Invalidate stage-1/stage-2 translations for the current VMID, not all
     * VMIDs. Runtime callers must establish the target VTTBR context and
     * synchronization; the startup call does not validate that protocol.
     * The leading dsb publishes table writes before invalidation.
     */
    asm volatile(
        "dsb ish\n"
        "tlbi vmalls12e1is\n"
        "dsb ish\n"
        "isb\n"
        :
        :
        : "memory"
    );
}
