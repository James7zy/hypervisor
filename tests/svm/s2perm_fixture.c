/* SPDX-License-Identifier: TBD */
#include <board.h>
#include <printk.h>
#include <vm.h>
#include <vm_config.h>
#include "stage2_build.h"
#include "s2perm_fixture.h"

#define RO_IPA 0x40210000ULL
#define XN_IPA 0x40211000ULL
#define OA_MASK 0x0000fffffffff000ULL

/* 独立逐叶比对：设备、OA、覆盖范围及非探针权限均不得改变。 */
static bool check_leaves(const u64 *table, u32 level, u64 base,
                         const struct vm *vm, u64 *ram, u64 *dev)
{
    const struct vm_config *c = vm->config;
    u64 span = 1ULL << (39 - 9 * level);
    for (u32 i = 0; i < 512; i++) {
        u64 d = table[i], ipa = base + i * span;
        if (!d)
            continue;
        if ((d & 3) == 3 && level < 3) {
            if (d != ((d & OA_MASK) | 3) ||
                !check_leaves((const u64 *)(uintptr_t)(d & OA_MASK),
                              level + 1, ipa, vm, ram, dev))
                return false;
            continue;
        }
        u64 expected;
        if (ipa < S2_L1_BLOCK) {
            if (level != 2 || ipa == (BOARD_GIC_DIST_BASE & ~(S2_L2_BLOCK - 1)) ||
                ipa == (BOARD_UART_BASE & ~(S2_L2_BLOCK - 1)))
                return false;
            expected = ipa | 1 | (1ULL << 2) | (3ULL << 6) |
                       (2ULL << 8) | (1ULL << 10) | (1ULL << 54);
            *dev += span;
        } else {
            if (ipa < c->mem_base || ipa + span > c->mem_base + c->mem_size ||
                (vm->id == 0 && level != 3))
                return false;
            expected = (c->ram_pa + ipa - c->mem_base) | (level == 3 ? 3 : 1) |
                       (15ULL << 2) | (3ULL << 6) | (3ULL << 8) | (1ULL << 10);
            if (vm->id == 0 && ipa == RO_IPA)
                expected &= ~(1ULL << 7);
            if (vm->id == 0 && ipa == XN_IPA)
                expected |= 1ULL << 54;
            *ram += span;
        }
        if (d != expected)
            return false;
    }
    return true;
}

int svm_s2perm_fixture(u64 *root, struct s2_pool *pool, const struct vm *vm)
{
    const struct vm_config *c = vm->config;
    bool flush = false;
    u64 ram = 0, dev = 0;
    if (!stage2_build_check(root, c))
        return S2_EINVAL;
    if (vm->id == 0) {
        if (c->mem_base != 0x40200000 || c->mem_size != S2_L2_BLOCK ||
            s2_unmap(root, pool, c->mem_base, c->mem_size, &flush))
            return S2_EINVAL;
        for (u64 offset = 0; offset < c->mem_size; offset += S2_PAGE_SIZE) {
            if (s2_map(root, pool, c->mem_base + offset, c->ram_pa + offset,
                       S2_PAGE_SIZE, S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush))
                return S2_EINVAL;
        }
        if (!stage2_build_check(root, c))
            return S2_EINVAL;
        if (s2_protect(root, pool, RO_IPA, S2_PAGE_SIZE,
                       S2_ATTR_NORMAL_WB | S2_PROT_RO, &flush) ||
            s2_protect(root, pool, XN_IPA, S2_PAGE_SIZE,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW | S2_XN, &flush))
            return S2_EINVAL;
    }
    if (!check_leaves(root, 1, 0, vm, &ram, &dev) || ram != c->mem_size ||
        dev != S2_L1_BLOCK - 2 * S2_L2_BLOCK)
        return S2_EINVAL;
    printk("[hv] s2perm fixture: vm=%u checked ro=0x%lx xn=0x%lx\n",
           (unsigned)vm->id, vm->id == 0 ? RO_IPA : 0, vm->id == 0 ? XN_IPA : 0);
    return 0;
}
