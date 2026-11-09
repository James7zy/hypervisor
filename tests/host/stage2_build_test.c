/* SPDX-License-Identifier: TBD */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vm_config.h>
#include <board.h>
#include "stage2_build.h"
#include "host_test.h"

int tests_run, tests_failed;
static u64 pages[S2_POOL_PAGES * 512] __attribute__((aligned(4096)));
static u64 root[512] __attribute__((aligned(4096)));

int printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

#define OA_MASK 0x0000fffffffff000ULL

/* Independent oracle: classify EVERY leaf by IPA, never by its MemAttr. */
static u64 check_leaves(const u64 *table, unsigned level, u64 base,
                        const struct vm_config *c, bool profile)
{
    u64 span = 1ULL << (39 - 9 * level);
    u64 ram_bytes = 0;
    for (unsigned i = 0; i < 512; i++) {
        u64 d = table[i], ipa = base + i * span;
        if (!(d & 1)) {
            TEST_ASSERT(d == 0);
            continue;
        }
        if (level < 3 && (d & 3) == 3) {
            ram_bytes += check_leaves((const u64 *)(uintptr_t)(d & OA_MASK),
                                      level + 1, ipa, c, profile);
            continue;
        }
        u64 pa = d & OA_MASK;
        u64 attrs = d & ~OA_MASK;
        if (ipa < S2_L1_BLOCK) {
            TEST_ASSERT(level == 2);
            TEST_ASSERT(pa == ipa);
            TEST_ASSERT(ipa != (BOARD_GIC_DIST_BASE & ~(S2_L2_BLOCK - 1)));
            TEST_ASSERT(ipa != (BOARD_UART_BASE & ~(S2_L2_BLOCK - 1)));
            TEST_ASSERT(attrs == (1ULL | (1ULL << 2) | (3ULL << 6) |
                                  (2ULL << 8) | (1ULL << 10) | (1ULL << 54)));
        } else {
            TEST_ASSERT(ipa >= c->mem_base && ipa + span <= c->mem_base + c->mem_size);
            TEST_ASSERT(pa == c->ram_pa + ipa - c->mem_base);
            TEST_ASSERT(attrs == ((level == 3 ? 3ULL : 1ULL) | (15ULL << 2) |
                                  (3ULL << 6) | (3ULL << 8) | (1ULL << 10)));
            if (profile) {
                TEST_ASSERT(pa + span <= 0x40080000ULL || pa >= BOARD_SVM_MEM_BASE);
                for (unsigned vm = 0; vm < NR_VMS; vm++) {
                    if (&vm_configs[vm] == c)
                        continue;
                    TEST_ASSERT(pa + span <= vm_configs[vm].ram_pa ||
                                pa >= vm_configs[vm].ram_pa + vm_configs[vm].mem_size);
                }
            }
            ram_bytes += span;
        }
    }
    return ram_bytes;
}

static bool mapped(u64 ipa)
{
    const u64 *table = root;
    for (unsigned level = 1; level <= 3; level++) {
        u64 d = table[(ipa >> (39 - 9 * level)) & 511];
        if (!(d & 1))
            return false;
        if (level == 3 || (d & 3) == 1)
            return true;
        table = (const u64 *)(uintptr_t)(d & OA_MASK);
    }
    return false;
}

static void check_ram(const struct vm_config *c, bool profile)
{
    TEST_ASSERT(check_leaves(root, 1, 0, c, profile) == c->mem_size);
    TEST_ASSERT(mapped(c->mem_base));
    TEST_ASSERT(mapped(c->mem_base + c->mem_size - S2_PAGE_SIZE));
    TEST_ASSERT(!mapped(c->mem_base + c->mem_size));
    if (c->mem_base > S2_L1_BLOCK)
        TEST_ASSERT(!mapped(c->mem_base - S2_PAGE_SIZE));
    TEST_ASSERT(!mapped(BOARD_GIC_DIST_BASE));
    TEST_ASSERT(!mapped(BOARD_GIC_RDIST_BASE));
    TEST_ASSERT(!mapped(BOARD_UART_BASE));
    TEST_ASSERT(mapped(BOARD_GIC_DIST_BASE - S2_PAGE_SIZE));
    TEST_ASSERT(mapped(BOARD_UART_BASE + S2_L2_BLOCK));
}

int main(void)
{
    struct s2_pool pool;

    for (u32 i = 0; i < NR_VMS; i++) {
        memset(root, 0, sizeof(root));
        TEST_ASSERT(s2_pool_init(&pool, pages, S2_POOL_PAGES) == 0);
        TEST_ASSERT(stage2_build(root, &pool, &vm_configs[i]) == 0);
        check_ram(&vm_configs[i], true);
    }
    printf("stage2_build: %d assertions, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
