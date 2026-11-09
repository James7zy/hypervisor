/* SPDX-License-Identifier: TBD */
#include <types.h>
#include <vm.h>
#include <board.h>
#include <printk.h>
#include <vm_config.h>   /* struct vm_config (vmid / ram_pa) */
#include <s2_table.h>
#include "el2_mmu.h"     /* MAIR_EL2 slot layout (EL2's own stage 1) */
#include "stage2.h"

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

/*
 * Reference descriptors: exactly what the pre-allocator static tables held, so
 * the rewrite is checked rather than trusted (HV-011 G6). RAM was one 1 GB
 * Normal block, the device 1 GB was 2 MB blocks except the GICD/GICR and PL011
 * windows, and every other L1 entry was invalid.
 */
#define REF_DEV_BLOCK(i) (((u64)(i) << 21) | 0x1ULL | (0x1ULL << 2) | \
                          (0x3ULL << 6) | (0x2ULL << 8) | (1ULL << 10) | (1ULL << 54))
#define REF_RAM_BLOCK(pa) ((pa) | 0x1ULL | (0xFULL << 2) | (0x3ULL << 6) | \
                           (0x3ULL << 8) | (1ULL << 10))
#define DESC_OA_BITS 0x0000FFFFFFFFF000ULL   /* descriptor bits [47:12] */

/* 4 KB table pages (HV-011 G1) and one L1 root per VM. VTTBR_EL2[11:0] are
 * reserved, so both must be 4 KB-aligned. */
static u64 s2_pool_mem[S2_POOL_PAGES * 512] __attribute__((aligned(4096)));
static struct s2_pool s2_pool;
static bool s2_pool_ready;
static u64 s2_root[NR_VMS][512] __attribute__((aligned(4096)));

/*
 * G6 self-check: walk the freshly built tables against the reference values
 * above. A mismatch names the entry and fails the VM's table build — the guest
 * never runs with a mapping nobody verified.
 */
static bool stage2_equiv_check(const u64 *root, u64 ram_pa)
{
    u32 gic  = (u32)(BOARD_GIC_DIST_BASE / S2_L2_BLOCK);
    u32 uart = (u32)(BOARD_UART_BASE / S2_L2_BLOCK);
    const u64 *l2;
    u64 want;

    if ((root[0] & 0x3ULL) != 0x3ULL) {
        printk("[hv] stage2: EQUIV FAIL L1[0]=0x%lx is not a table\n", root[0]);
        return false;
    }
    l2 = (const u64 *)(uintptr_t)(root[0] & DESC_OA_BITS);

    for (u32 i = 0; i < 512U; i++) {
        want = (i == gic || i == uart) ? 0ULL : REF_DEV_BLOCK(i);
        if (l2[i] != want) {
            printk("[hv] stage2: EQUIV FAIL l2[%u]=0x%lx want 0x%lx\n",
                   (unsigned)i, l2[i], want);
            return false;
        }
    }

    want = REF_RAM_BLOCK(ram_pa);
    if (root[1] != want) {
        printk("[hv] stage2: EQUIV FAIL L1[1]=0x%lx want 0x%lx\n",
               root[1], want);
        return false;
    }

    for (u32 i = 2; i < 512U; i++) {
        if (root[i] != 0ULL) {
            printk("[hv] stage2: EQUIV FAIL L1[%u]=0x%lx want 0x0\n",
                   (unsigned)i, root[i]);
            return false;
        }
    }
    return true;
}

int stage2_init(struct vm *m)
{
    u64 *root = s2_root[m->id];
    /*
     * An L1 block covers 1 GB, so its output address MUST be 1 GB-aligned.
     * BOARD_LINUX_RAM_PA is; the SVM profiles' ram_pa (0x40200000) is not and
     * relies on this mask exactly as the pre-allocator table did (guest IPA
     * 0x40200000 resolves to identity PA 0x40200000 through the masked base).
     * The mask asserts that invariant; it is not a rounding step.
     */
    u64 ram_pa = (u64)m->config->ram_pa & ~(S2_L1_BLOCK - 1ULL);
    u32 dev_attr = S2_ATTR_DEVICE_nGnRE | S2_PROT_RW | S2_XN;
    u64 gic_base  = (u64)(BOARD_GIC_DIST_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    u64 uart_base = (u64)(BOARD_UART_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    bool flush = false;
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

    /*
     * Device 1 GB (IPA 0x00000000-0x3FFFFFFF) as identity 2 MB blocks, with
     * the GICD/GICR and PL011 2 MB windows left UNMAPPED: guest accesses there
     * fault into EL2 → MMIO trap → vgic_v3_mmio / vuart shadow emulation
     * (ADR-0012, ADR-0014). One L2 table serves all three ranges.
     */
    if (gic_base != (u64)(BOARD_GIC_RDIST_BASE / S2_L2_BLOCK) * S2_L2_BLOCK)
        printk("[hv] stage2: WARN GICD/GICR span >1 L2 entry (D=%u R=%u); "
               "only D's 2 MB window is unmapped\n",
               (unsigned)(gic_base / S2_L2_BLOCK),
               (unsigned)(BOARD_GIC_RDIST_BASE / S2_L2_BLOCK));

    rc = s2_map(root, &s2_pool, 0, 0, gic_base, dev_attr, &flush);
    if (rc == 0)
        rc = s2_map(root, &s2_pool, gic_base + S2_L2_BLOCK, gic_base + S2_L2_BLOCK,
                    uart_base - gic_base - S2_L2_BLOCK, dev_attr, &flush);
    if (rc == 0)
        rc = s2_map(root, &s2_pool, uart_base + S2_L2_BLOCK, uart_base + S2_L2_BLOCK,
                    S2_L1_BLOCK - uart_base - S2_L2_BLOCK, dev_attr, &flush);

    /*
     * IPA 0x40000000-0x7FFFFFFF → PA ram_pa: one 1 GB Normal WB block, the
     * non-identity mapping the Linux guest boots from (ram_pa does not overlap
     * the hv image at 0x40080000).
     */
    if (rc == 0)
        rc = s2_map(root, &s2_pool, BOARD_LINUX_RAM_IPA, ram_pa, S2_L1_BLOCK,
                    S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush);

    if (rc == 0 && !stage2_equiv_check(root, ram_pa))
        rc = S2_EINVAL;

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
