/* SPDX-License-Identifier: TBD */
/*
 * Stage-2 table pool and descriptor encoder (HV-011 G1/G2). Generic: the
 * descriptor format below is the ARMv8-A stage-2 one, but nothing here reads
 * a register or includes an arch header, so the pool, the walk and the
 * encoding are all host-testable. TLB maintenance belongs to the caller.
 */
#include <s2_table.h>

/* Stage-2 block/page descriptor fields (ARM DDI0487 D8.2.6). */
#define D_VALID     (1ULL << 0)
#define D_BLOCK     (1ULL << 0)             /* [1:0]=0b01: L1/L2 block  */
#define D_TABLE     (3ULL << 0)             /* [1:0]=0b11: table or L3 page */
#define D_OA_MASK   0x0000FFFFFFFFF000ULL
#define D_MEMATTR(v) ((u64)(v) << 2)
#define D_S2AP(v)    ((u64)(v) << 6)
#define D_SH(v)      ((u64)(v) << 8)
#define D_AF         (1ULL << 10)
#define D_XN         (1ULL << 54)
/* Everything s2_protect() may rewrite; the output address and the type bits
 * are not part of it. */
#define D_ATTR_MASK  (D_MEMATTR(0xF) | D_S2AP(3) | D_SH(3) | D_AF | D_XN)

#define PAGE_SHIFT   12
#define TABLE_ENTRIES 512U
#define IPA_LIMIT    (1ULL << 39)           /* VTCR_EL2.T0SZ=25 */

/* Per-selector (MemAttr[3:0], SH[1:0]) from DDI0487's stage-2 attribute
 * table: 0xF = Normal WB RW-allocate, 0x1 = Device-nGnRE. */
static const struct {
    u8 memattr;
    u8 sh;
} s2_attr_bits[2] = {
    [S2_ATTR_NORMAL_WB]    = { 0xFU, 0x3U },   /* Inner Shareable */
    [S2_ATTR_DEVICE_nGnRE] = { 0x1U, 0x2U },   /* Outer Shareable */
};

/* Block/page descriptor for `pa` with the attribute word `attr`. */
static u64 s2_encode(u64 pa, u32 attr)
{
    u32 sel = attr & S2_ATTR_MASK;
    u64 d = (pa & D_OA_MASK) |
            D_MEMATTR(s2_attr_bits[sel].memattr) |
            D_S2AP((attr & S2_PROT_MASK) >> 2) |
            D_SH(s2_attr_bits[sel].sh) |
            D_AF;

    if ((attr & S2_XN) != 0U)
        d |= D_XN;
    return d;
}

int s2_pool_init(struct s2_pool *p, void *mem, u32 npages)
{
    if (npages == 0U || npages > (u32)S2_POOL_PAGES)
        return S2_EINVAL;
    if (((uintptr_t)mem & (S2_PAGE_SIZE - 1ULL)) != 0U)
        return S2_EINVAL;

    p->pages  = mem;
    p->npages = npages;
    for (u32 i = 0; i < (u32)((S2_POOL_PAGES + 63) / 64); i++)
        p->bitmap[i] = 0;
    return 0;
}

u64 *s2_pool_alloc(struct s2_pool *p)
{
    for (u32 i = 0; i < p->npages; i++) {
        u64 bit = 1ULL << (i % 64U);

        if ((p->bitmap[i / 64U] & bit) == 0U) {
            u64 *t = &p->pages[(u64)i * TABLE_ENTRIES];

            p->bitmap[i / 64U] |= bit;
            /* A recycled page must not resurrect the old owner's entries. */
            for (u32 j = 0; j < TABLE_ENTRIES; j++)
                t[j] = 0;
            return t;
        }
    }
    return NULL;
}

void s2_pool_free(struct s2_pool *p, u64 *table)
{
    uintptr_t off = (uintptr_t)table - (uintptr_t)p->pages;

    /* Not a page of this pool: nothing to give back (a double free must not
     * hand out someone else's table). */
    if ((uintptr_t)table < (uintptr_t)p->pages || (off & (S2_PAGE_SIZE - 1ULL)) != 0U)
        return;
    u32 i = (u32)(off >> PAGE_SHIFT);
    if (i >= p->npages)
        return;
    p->bitmap[i / 64U] &= ~(1ULL << (i % 64U));
}

/* The sub-table recorded at *entry, allocated and installed when the entry is
 * invalid. NULL with *rc set when the entry holds a block (no splitting in
 * this slice) or the pool is exhausted. */
static u64 *s2_child(u64 *entry, struct s2_pool *p, int *rc)
{
    if ((*entry & D_VALID) != 0U) {
        if ((*entry & 3ULL) != D_TABLE) {
            *rc = S2_EINVAL;
            return NULL;
        }
        return (u64 *)(uintptr_t)(*entry & D_OA_MASK);
    }

    u64 *t = s2_pool_alloc(p);
    if (t == NULL) {
        *rc = S2_ENOMEM;
        return NULL;
    }
    *entry = (u64)(uintptr_t)t | D_TABLE;
    *rc = 0;
    return t;
}

/* Map one block/page: `len` is exactly the granule, ipa and pa are aligned to
 * it. */
static int s2_map_leaf(u64 *root, struct s2_pool *p, u64 ipa, u64 pa, u64 len,
                       u32 attr, bool *flush)
{
    int rc = 0;
    u64 *l2;
    u64 *l3;
    u64 *slot;

    if (len == S2_L1_BLOCK) {
        slot = &root[(ipa >> 30) & (TABLE_ENTRIES - 1U)];
    } else {
        l2 = s2_child(&root[(ipa >> 30) & (TABLE_ENTRIES - 1U)], p, &rc);
        if (l2 == NULL)
            return rc;
        slot = &l2[(ipa >> 21) & (TABLE_ENTRIES - 1U)];
        if (len != S2_L2_BLOCK) {
            l3 = s2_child(slot, p, &rc);
            if (l3 == NULL)
                return rc;
            slot = &l3[(ipa >> PAGE_SHIFT) & (TABLE_ENTRIES - 1U)];
        }
    }

    if ((*slot & D_VALID) != 0U)
        return S2_EINVAL;                    /* already mapped */

    bool block = (len == S2_L1_BLOCK) || (len == S2_L2_BLOCK);
    *slot = s2_encode(pa, attr) | (block ? D_BLOCK : D_TABLE);
    *flush = true;
    return 0;
}

/* Largest granule that fits this ipa/pa/len triple (both addresses aligned). */
static u64 s2_leaf_size(u64 ipa, u64 pa, u64 len)
{
    if ((ipa & (S2_L1_BLOCK - 1ULL)) == 0U && (pa & (S2_L1_BLOCK - 1ULL)) == 0U &&
        len >= S2_L1_BLOCK)
        return S2_L1_BLOCK;
    if ((ipa & (S2_L2_BLOCK - 1ULL)) == 0U && (pa & (S2_L2_BLOCK - 1ULL)) == 0U &&
        len >= S2_L2_BLOCK)
        return S2_L2_BLOCK;
    return S2_PAGE_SIZE;
}

int s2_map(u64 *root, struct s2_pool *p, u64 ipa, u64 pa, u64 len, u32 attr,
           bool *flush)
{
    if ((ipa & (S2_PAGE_SIZE - 1ULL)) != 0U || (pa & (S2_PAGE_SIZE - 1ULL)) != 0U ||
        (len & (S2_PAGE_SIZE - 1ULL)) != 0U || len == 0U ||
        ipa >= IPA_LIMIT || len > IPA_LIMIT - ipa)
        return S2_EINVAL;

    while (len != 0U) {
        u64 granule = s2_leaf_size(ipa, pa, len);
        int rc = s2_map_leaf(root, p, ipa, pa, granule, attr, flush);

        if (rc != 0)
            return rc;
        ipa += granule;
        pa  += granule;
        len -= granule;
    }
    return 0;
}

/* Entry for ipa, with the granule it is mapped at in *granule. NULL when the
 * ipa is unmapped. */
static u64 *s2_leaf_at(u64 *root, u64 ipa, u64 *granule)
{
    u64 *slot = &root[(ipa >> 30) & (TABLE_ENTRIES - 1U)];

    if ((*slot & D_VALID) == 0U)
        return NULL;
    if ((*slot & 3ULL) == D_BLOCK) {
        *granule = S2_L1_BLOCK;
        return slot;
    }

    u64 *l2 = (u64 *)(uintptr_t)(*slot & D_OA_MASK);
    slot = &l2[(ipa >> 21) & (TABLE_ENTRIES - 1U)];
    if ((*slot & D_VALID) == 0U)
        return NULL;
    if ((*slot & 3ULL) == D_BLOCK) {
        *granule = S2_L2_BLOCK;
        return slot;
    }

    u64 *l3 = (u64 *)(uintptr_t)(*slot & D_OA_MASK);
    slot = &l3[(ipa >> PAGE_SHIFT) & (TABLE_ENTRIES - 1U)];
    if ((*slot & D_VALID) == 0U)
        return NULL;
    *granule = S2_PAGE_SIZE;
    return slot;
}

/* The entry unmapping/protecting `ipa` may touch: it must exist, and the block
 * or page it covers must lie wholly inside [ipa, ipa+len) — this slice has no
 * block splitting, so a range that starts or ends inside a block is refused
 * instead of reshaping it. */
static u64 *s2_leaf_cover(u64 *root, u64 ipa, u64 len, u64 *granule)
{
    u64 *slot = s2_leaf_at(root, ipa, granule);

    if (slot == NULL || (ipa & (*granule - 1ULL)) != 0U || len < *granule)
        return NULL;
    return slot;
}

static bool s2_table_empty(const u64 *t)
{
    for (u32 i = 0; i < TABLE_ENTRIES; i++)
        if (t[i] != 0U)
            return false;
    return true;
}

/* Drop a table page that unmapping just emptied, and the parent entry with
 * it, so a re-map of the same range reuses the page instead of leaking it. */
static void s2_reclaim(u64 *parent, u32 idx, struct s2_pool *p)
{
    if ((parent[idx] & 3ULL) != D_TABLE)
        return;

    u64 *t = (u64 *)(uintptr_t)(parent[idx] & D_OA_MASK);

    if (!s2_table_empty(t))
        return;
    s2_pool_free(p, t);
    parent[idx] = 0;
}

int s2_unmap(u64 *root, struct s2_pool *p, u64 ipa, u64 len, bool *flush)
{
    if ((ipa & (S2_PAGE_SIZE - 1ULL)) != 0U || (len & (S2_PAGE_SIZE - 1ULL)) != 0U ||
        len == 0U || ipa >= IPA_LIMIT || len > IPA_LIMIT - ipa)
        return S2_EINVAL;

    while (len != 0U) {
        u64 granule;
        u64 *slot = s2_leaf_cover(root, ipa, len, &granule);

        if (slot == NULL)
            return S2_EINVAL;                /* not mapped as asked */

        *slot = 0;
        *flush = true;

        /* Walk back up: an empty L3 frees its L2 slot, an empty L2 its L1. */
        if (granule == S2_PAGE_SIZE) {
            u32 l1 = (u32)((ipa >> 30) & (TABLE_ENTRIES - 1U));
            u64 *l2 = (u64 *)(uintptr_t)(root[l1] & D_OA_MASK);

            s2_reclaim(l2, (u32)((ipa >> 21) & (TABLE_ENTRIES - 1U)), p);
        }
        if (granule != S2_L1_BLOCK)
            s2_reclaim(root, (u32)((ipa >> 30) & (TABLE_ENTRIES - 1U)), p);

        ipa += granule;
        len -= granule;
    }
    return 0;
}

int s2_protect(u64 *root, struct s2_pool *p, u64 ipa, u64 len, u32 attr,
               bool *flush)
{
    u64 want;

    (void)p;   /* protect never allocates or frees a table page */
    if ((ipa & (S2_PAGE_SIZE - 1ULL)) != 0U || (len & (S2_PAGE_SIZE - 1ULL)) != 0U ||
        len == 0U || ipa >= IPA_LIMIT || len > IPA_LIMIT - ipa)
        return S2_EINVAL;

    while (len != 0U) {
        u64 granule;
        u64 *slot = s2_leaf_cover(root, ipa, len, &granule);

        if (slot == NULL)
            return S2_EINVAL;

        /* Output address and type bits survive: only the attribute half of
         * the descriptor is rewritten. */
        want = (*slot & ~D_ATTR_MASK) | s2_encode(0, attr);
        if (want != *slot) {
            *slot = want;
            *flush = true;
        }
        ipa += granule;
        len -= granule;
    }
    return 0;
}
