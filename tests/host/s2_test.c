/* SPDX-License-Identifier: TBD */
/*
 * Stage-2 table pool + map/unmap/protect (HV-011 T1). Everything the guest
 * sees about a Stage-2 mapping is a descriptor in memory, so the whole API is
 * asserted here; the arch side only adds registers and TLB maintenance
 * (arch/arm64/mmu/stage2.c).
 */
#include <s2_table.h>
#include "host_test.h"

int tests_run;
int tests_failed;

/*
 * Descriptor bits asserted literally, from ARM DDI0487 D8.2.6 (stage-2 block
 * and page descriptor): MemAttr[3:0] at [5:2], S2AP[1:0] at [7:6], SH[1:0] at
 * [9:8], AF at [10], output address at [47:12], XN at [54]. Changing the
 * encoding changes what the guest sees, so the test spells the values out
 * instead of reusing the implementation's macros.
 */
#define DESC_BLOCK    0x1ULL
#define DESC_TABLE    0x3ULL            /* [1:0]=0b11 at L1/L2 = table */
#define DESC_PAGE     0x3ULL            /* [1:0]=0b11 at L3 = page     */
#define DESC_DEV      (0x1ULL << 2)     /* Device-nGnRE                */
#define DESC_NORM     (0xFULL << 2)     /* Normal WB, RW-allocate      */
#define DESC_S2AP_RW  (0x3ULL << 6)
#define DESC_S2AP_RO  (0x1ULL << 6)
#define DESC_SH_OSH   (0x2ULL << 8)
#define DESC_SH_ISH   (0x3ULL << 8)
#define DESC_AF       (1ULL << 10)
#define DESC_XN       (1ULL << 54)
#define DESC_OA(d)    ((d) & 0x0000FFFFFFFFF000ULL)

#define PAGE_4K   0x1000ULL
#define BLOCK_2M  0x200000ULL
#define BLOCK_1G  0x40000000ULL

static u64 pool_mem[8 * 512] __attribute__((aligned(4096)));
static u64 root[512] __attribute__((aligned(4096)));
static struct s2_pool pool;

/* Fresh empty root + an 8-page pool. */
static void reset(void)
{
    for (u32 i = 0; i < 512U; i++)
        root[i] = 0;
    TEST_ASSERT(s2_pool_init(&pool, pool_mem, 8) == 0);
}

/* Pages still free, measured by draining and giving back. */
static u32 pool_free_pages(struct s2_pool *p)
{
    u64 *taken[S2_POOL_PAGES];
    u32 n = 0;

    while (n < (u32)S2_POOL_PAGES && (taken[n] = s2_pool_alloc(p)) != NULL)
        n++;
    for (u32 i = 0; i < n; i++)
        s2_pool_free(p, taken[i]);
    return n;
}

static void pool_case(void)
{
    struct s2_pool p;
    u64 *a, *b, *c;

    /* 0 pages or more than the struct can track is a configuration bug, not
     * a smaller pool: reject, never clamp. */
    TEST_ASSERT(s2_pool_init(&p, pool_mem, 0) == S2_EINVAL);
    TEST_ASSERT(s2_pool_init(&p, pool_mem, S2_POOL_PAGES + 1) == S2_EINVAL);
    /* Table addresses come from descriptor bits [47:12]: 4 KB alignment is
     * not negotiable. */
    TEST_ASSERT(s2_pool_init(&p, (u8 *)pool_mem + 8, 4) == S2_EINVAL);

    TEST_ASSERT(s2_pool_init(&p, pool_mem, 3) == 0);
    a = s2_pool_alloc(&p);
    b = s2_pool_alloc(&p);
    c = s2_pool_alloc(&p);
    TEST_ASSERT(a != NULL && b != NULL && c != NULL);
    TEST_ASSERT(a != b && b != c && a != c);
    TEST_ASSERT(s2_pool_alloc(&p) == NULL);         /* exhausted, not wrapped */

    /* A freed page is reusable and comes back zeroed: no leak, and no
     * previous owner's descriptors left behind. */
    a[0] = 0xDEADULL;
    s2_pool_free(&p, a);
    TEST_ASSERT(s2_pool_alloc(&p) == a);
    TEST_ASSERT(a[0] == 0);
    TEST_ASSERT(s2_pool_alloc(&p) == NULL);

    s2_pool_free(&p, b);
    s2_pool_free(&p, c);
    s2_pool_free(&p, a);
    TEST_ASSERT(pool_free_pages(&p) == 3);
}

/* A 128 MB device range with 2 MB granularity: one L2 table, 64 blocks. */
static void map_block_case(void)
{
    const u64 want = DESC_BLOCK | DESC_DEV | DESC_S2AP_RW | DESC_SH_OSH |
                     DESC_AF | DESC_XN;
    bool flush = false;
    u64 *l2;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0, 0, 64ULL * BLOCK_2M,
                       S2_ATTR_DEVICE_nGnRE | S2_PROT_RW | S2_XN, &flush) == 0);
    TEST_ASSERT(flush);                     /* the guest must see the change */
    TEST_ASSERT((root[0] & 0x3ULL) == DESC_TABLE);
    TEST_ASSERT(root[1] == 0);              /* only the requested range */

    l2 = (u64 *)(uintptr_t)DESC_OA(root[0]);
    TEST_ASSERT(l2[0] == (0ULL | want));
    TEST_ASSERT(l2[1] == (BLOCK_2M | want));
    TEST_ASSERT(l2[63] == (63ULL * BLOCK_2M | want));
    TEST_ASSERT(l2[64] == 0);               /* past the range: unmapped */
    TEST_ASSERT(pool_free_pages(&pool) == 7);   /* exactly one table page */
}

/* A 1 GB-aligned range stays a level-1 block: no table page at all. */
static void map_1g_case(void)
{
    bool flush = false;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0x40000000ULL, 0x80000000ULL, BLOCK_1G,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == 0);
    TEST_ASSERT(flush);
    TEST_ASSERT(root[0] == 0);
    TEST_ASSERT(root[1] == (0x80000000ULL | DESC_BLOCK | DESC_NORM |
                            DESC_S2AP_RW | DESC_SH_ISH | DESC_AF));
    TEST_ASSERT(root[2] == 0);
    TEST_ASSERT(pool_free_pages(&pool) == 8);
}

/* A single 4 KB page needs L2 + L3; unmap gives both pages back. */
static void map_page_case(void)
{
    const u64 want = DESC_PAGE | DESC_NORM | DESC_S2AP_RW | DESC_SH_ISH |
                     DESC_AF | DESC_XN;
    bool flush = false;
    u64 *l2, *l3;

    reset();
    TEST_ASSERT(s2_map(root, &pool, BLOCK_2M, BLOCK_2M, PAGE_4K,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW | S2_XN, &flush) == 0);
    TEST_ASSERT(flush);
    TEST_ASSERT((root[0] & 0x3ULL) == DESC_TABLE);
    l2 = (u64 *)(uintptr_t)DESC_OA(root[0]);
    TEST_ASSERT((l2[1] & 0x3ULL) == DESC_TABLE);
    l3 = (u64 *)(uintptr_t)DESC_OA(l2[1]);
    TEST_ASSERT(l3[0] == (BLOCK_2M | want));
    TEST_ASSERT(pool_free_pages(&pool) == 6);

    /* Unmap reclaims both table pages: the L1 entry is invalid again. */
    flush = false;
    TEST_ASSERT(s2_unmap(root, &pool, BLOCK_2M, PAGE_4K, &flush) == 0);
    TEST_ASSERT(flush);
    TEST_ASSERT(root[0] == 0);
    TEST_ASSERT(pool_free_pages(&pool) == 8);
    /* Unmapping what is not there is an error, not a silent success. */
    TEST_ASSERT(s2_unmap(root, &pool, BLOCK_2M, PAGE_4K, &flush) == S2_EINVAL);
}

static void protect_case(void)
{
    const u64 rw = 0x80000000ULL | DESC_BLOCK | DESC_NORM | DESC_S2AP_RW |
                   DESC_SH_ISH | DESC_AF;
    bool flush = false;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0x40000000ULL, 0x80000000ULL, BLOCK_1G,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == 0);
    TEST_ASSERT(root[1] == rw);

    /* Write protection is S2AP, and the output address never moves. */
    TEST_ASSERT(s2_protect(root, &pool, 0x40000000ULL, BLOCK_1G,
                           S2_ATTR_NORMAL_WB | S2_PROT_RO | S2_XN,
                           &flush) == 0);
    TEST_ASSERT(flush);
    TEST_ASSERT(DESC_OA(root[1]) == 0x80000000ULL);
    TEST_ASSERT((root[1] & (0x3ULL << 6)) == DESC_S2AP_RO);
    TEST_ASSERT((root[1] & DESC_XN) == DESC_XN);
    TEST_ASSERT((root[1] & 0x3ULL) == DESC_BLOCK);

    /* Same attributes again: nothing changed, nothing to flush. */
    flush = false;
    TEST_ASSERT(s2_protect(root, &pool, 0x40000000ULL, BLOCK_1G,
                           S2_ATTR_NORMAL_WB | S2_PROT_RO | S2_XN,
                           &flush) == 0);
    TEST_ASSERT(!flush);

    /* Unmapped range: error. Partially mapped range: error (see no_split). */
    TEST_ASSERT(s2_protect(root, &pool, 0x00100000ULL, PAGE_4K,
                           S2_ATTR_NORMAL_WB | S2_PROT_RO, &flush) == S2_EINVAL);
}

/* HV-011 G6-limited scope: no block splitting, so a page-range request that
 * lands inside an existing block is refused, never silently reshaped. */
static void no_split_case(void)
{
    bool flush = false;
    u64 before;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0x40000000ULL, 0x80000000ULL, BLOCK_1G,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == 0);
    before = root[1];
    TEST_ASSERT(s2_map(root, &pool, 0x40100000ULL, 0x80100000ULL, PAGE_4K,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == S2_EINVAL);
    TEST_ASSERT(s2_unmap(root, &pool, 0x40100000ULL, PAGE_4K, &flush) == S2_EINVAL);
    TEST_ASSERT(root[1] == before);          /* untouched by the refusals */
}

/* Blocks and pages in one call: the first 2 MB as a block, the rest as pages. */
static void mixed_case(void)
{
    bool flush = false;
    u64 *l2, *l3;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0, 0, BLOCK_2M + PAGE_4K * 2ULL,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == 0);
    l2 = (u64 *)(uintptr_t)DESC_OA(root[0]);
    TEST_ASSERT((l2[0] & 0x3ULL) == DESC_BLOCK);
    TEST_ASSERT((l2[1] & 0x3ULL) == DESC_TABLE);
    l3 = (u64 *)(uintptr_t)DESC_OA(l2[1]);
    TEST_ASSERT(l3[0] == (BLOCK_2M | DESC_PAGE | DESC_NORM | DESC_S2AP_RW |
                          DESC_SH_ISH | DESC_AF));
    TEST_ASSERT(l3[1] == ((BLOCK_2M + PAGE_4K) | DESC_PAGE | DESC_NORM |
                          DESC_S2AP_RW | DESC_SH_ISH | DESC_AF));
    TEST_ASSERT(l3[2] == 0);
    TEST_ASSERT(pool_free_pages(&pool) == 6);
}

/* Pool exhaustion surfaces as S2_ENOMEM at the caller, not as a mapping that
 * silently keeps part of the range. */
static void exhaustion_case(void)
{
    struct s2_pool p;
    bool flush = false;

    for (u32 i = 0; i < 512U; i++)
        root[i] = 0;
    TEST_ASSERT(s2_pool_init(&p, pool_mem, 2) == 0);
    TEST_ASSERT(s2_map(root, &p, 0, 0, PAGE_4K,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == 0);
    TEST_ASSERT(s2_map(root, &p, BLOCK_1G, BLOCK_1G, PAGE_4K,
                       S2_ATTR_NORMAL_WB | S2_PROT_RW, &flush) == S2_ENOMEM);
    TEST_ASSERT(root[1] == 0);
}

static void arg_case(void)
{
    bool flush = false;

    reset();
    TEST_ASSERT(s2_map(root, &pool, 0, 0, 0, S2_ATTR_NORMAL_WB,
                       &flush) == S2_EINVAL);
    TEST_ASSERT(s2_map(root, &pool, 0x800, 0, PAGE_4K, S2_ATTR_NORMAL_WB,
                       &flush) == S2_EINVAL);
    TEST_ASSERT(s2_map(root, &pool, 0, 0, PAGE_4K + 1, S2_ATTR_NORMAL_WB,
                       &flush) == S2_EINVAL);
    /* 39-bit IPA space (VTCR_EL2.T0SZ=25): past the end is not an address. */
    TEST_ASSERT(s2_map(root, &pool, 0x7FFFF00000ULL, 0, BLOCK_2M,
                       S2_ATTR_NORMAL_WB, &flush) == S2_EINVAL);
    TEST_ASSERT(root[0] == 0);
}

int main(void)
{
    pool_case();
    map_block_case();
    map_1g_case();
    map_page_case();
    protect_case();
    no_split_case();
    mixed_case();
    exhaustion_case();
    arg_case();

    printf("s2 table %s (%d assertions)\n",
           tests_failed ? "FAIL" : "PASS", tests_run);
    return tests_failed != 0;
}
