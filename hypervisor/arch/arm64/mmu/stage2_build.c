/* SPDX-License-Identifier: TBD */
#include <board.h>
#include <printk.h>
#include <vm_config.h>
#include "stage2_build.h"

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

int stage2_build(u64 *root, struct s2_pool *pool, const struct vm_config *config)
{
    /*
     * An L1 block covers 1 GB, so its output address MUST be 1 GB-aligned.
     * BOARD_LINUX_RAM_PA is; the SVM profiles' ram_pa (0x40200000) is not and
     * relies on this mask exactly as the pre-allocator table did (guest IPA
     * 0x40200000 resolves to identity PA 0x40200000 through the masked base).
     * The mask asserts that invariant; it is not a rounding step.
     */
    u64 ram_pa = (u64)config->ram_pa & ~(S2_L1_BLOCK - 1ULL);
    u32 dev_attr = S2_ATTR_DEVICE_nGnRE | S2_PROT_RW | S2_XN;
    u64 gic_base  = (u64)(BOARD_GIC_DIST_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    u64 uart_base = (u64)(BOARD_UART_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    bool flush = false;
    int rc;

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

    rc = s2_map(root, pool, 0, 0, gic_base, dev_attr, &flush);
    if (rc == 0)
        rc = s2_map(root, pool, gic_base + S2_L2_BLOCK, gic_base + S2_L2_BLOCK,
                    uart_base - gic_base - S2_L2_BLOCK, dev_attr, &flush);
    if (rc == 0)
        rc = s2_map(root, pool, uart_base + S2_L2_BLOCK, uart_base + S2_L2_BLOCK,
                    S2_L1_BLOCK - uart_base - S2_L2_BLOCK, dev_attr, &flush);

    /*
     * IPA 0x40000000-0x7FFFFFFF → PA ram_pa: one 1 GB Normal WB block, the
     * non-identity mapping the Linux guest boots from (ram_pa does not overlap
     * the hv image at 0x40080000).
     */
    if (rc == 0)
        rc = s2_map(root, pool, BOARD_LINUX_RAM_IPA, ram_pa, S2_L1_BLOCK,
                    S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush);

    if (rc == 0 && !stage2_equiv_check(root, ram_pa))
        rc = S2_EINVAL;

    return rc;
}
