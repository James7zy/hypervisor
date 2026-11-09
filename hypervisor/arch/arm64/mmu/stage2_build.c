/* SPDX-License-Identifier: TBD */
#include <board.h>
#include <printk.h>
#include <vm_config.h>
#include "stage2_build.h"

/* Device descriptors retain the existing layout, including the trap holes. */
#define REF_DEV_BLOCK(i) (((u64)(i) << 21) | 0x1ULL | (0x1ULL << 2) | \
                          (0x3ULL << 6) | (0x2ULL << 8) | (1ULL << 10) | (1ULL << 54))
#define REF_RAM_BLOCK(pa) ((pa) | 0x1ULL | (0xFULL << 2) | (0x3ULL << 6) | \
                           (0x3ULL << 8) | (1ULL << 10))
#define DESC_OA_BITS 0x0000FFFFFFFFF000ULL   /* descriptor bits [47:12] */

static bool ram_config_valid(const struct vm_config *c)
{
    const u64 ipa_limit = 1ULL << 39, pa_limit = 1ULL << 40;

    /* Subtract before adding: endpoints may equal the VTCR address limits. */
    return c && c->mem_size &&
           !((c->mem_base | c->ram_pa | c->mem_size) & (S2_PAGE_SIZE - 1)) &&
           c->mem_base >= S2_L1_BLOCK && c->mem_base < ipa_limit &&
           c->mem_size <= ipa_limit - c->mem_base &&
           c->ram_pa < pa_limit && c->mem_size <= pa_limit - c->ram_pa;
}

/* At most three levels; every leaf must be authorized, regardless of MemAttr. */
static bool check_table(const u64 *table, u32 level, u64 base,
                        const struct vm_config *c, u64 *ram_bytes, u64 *dev_bytes)
{
    u64 span = 1ULL << (39 - 9 * level);
    for (u32 i = 0; i < 512; i++) {
        u64 d = table[i], ipa = base + i * span;
        if (!d)
            continue;
        if ((d & 3) == 3 && level < 3) {
            if (d != ((d & DESC_OA_BITS) | 3ULL) ||
                !check_table((const u64 *)(uintptr_t)(d & DESC_OA_BITS),
                             level + 1, ipa, c, ram_bytes, dev_bytes))
                return false;
        } else if (ipa < S2_L1_BLOCK) {
            if (level != 2 || ipa == (BOARD_GIC_DIST_BASE & ~(S2_L2_BLOCK - 1)) ||
                ipa == (BOARD_UART_BASE & ~(S2_L2_BLOCK - 1)) ||
                d != REF_DEV_BLOCK(ipa / S2_L2_BLOCK))
                return false;
            *dev_bytes += span;
        } else {
            if (ipa < c->mem_base || ipa + span > c->mem_base + c->mem_size ||
                d != (REF_RAM_BLOCK(c->ram_pa + ipa - c->mem_base) |
                      (level == 3 ? 2ULL : 0ULL)))
                return false;
            *ram_bytes += span;
        }
    }
    return true;
}

bool stage2_build_check(const u64 *root, const struct vm_config *config)
{
    u64 ram_bytes = 0, dev_bytes = 0;
    return root && ram_config_valid(config) &&
           check_table(root, 1, 0, config, &ram_bytes, &dev_bytes) &&
           ram_bytes == config->mem_size &&
           dev_bytes == S2_L1_BLOCK - 2 * S2_L2_BLOCK;
}

int stage2_build(u64 *root, struct s2_pool *pool, const struct vm_config *config)
{
    u32 dev_attr = S2_ATTR_DEVICE_nGnRE | S2_PROT_RW | S2_XN;
    u64 gic_base  = (u64)(BOARD_GIC_DIST_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    u64 uart_base = (u64)(BOARD_UART_BASE / S2_L2_BLOCK) * S2_L2_BLOCK;
    bool flush = false;
    int rc;

    if (!ram_config_valid(config))
        return S2_EINVAL;

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

    if (rc == 0)
        rc = s2_map(root, pool, config->mem_base, config->ram_pa, config->mem_size,
                    S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush);

    if (rc == 0 && !stage2_build_check(root, config)) {
        printk("[hv] stage2: RAM bounds check failed\n");
        rc = S2_EINVAL;
    }

    return rc;
}
