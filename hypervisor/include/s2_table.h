/* SPDX-License-Identifier: TBD */
#ifndef HV_S2_TABLE_H
#define HV_S2_TABLE_H

#include <types.h>

/*
 * Stage-2 translation tables: a 4 KB table-page pool plus the map/unmap/
 * protect API (HV-011). Pure descriptor arithmetic — no arch headers, no
 * registers — so the whole encoding is host-testable. The arch side owns the
 * pool memory, installs the root in VTTBR_EL2 and performs the TLB
 * maintenance requested through *flush.
 *
 * Address layout it builds for (VTCR_EL2 T0SZ=25, SL0=1, 4 KB granule):
 * 39-bit IPA space, level 1 = 1 GB blocks, level 2 = 2 MB blocks, level 3 =
 * 4 KB pages.
 */

/* Return codes. No errno in a freestanding build; these are the -EINVAL /
 * -ENOMEM of the design (G1). */
#define S2_EINVAL  (-1)   /* unaligned/zero/out-of-range, or a mapping that
                             would need a block split (G6-limit) */
#define S2_ENOMEM  (-2)   /* table-page pool exhausted */

/* Upper bound on the pages one pool may be handed. */
#define S2_POOL_PAGES 64

/* Granules of the layout above. The arch side needs them to express its
 * block-aligned windows (e.g. which 2 MB of a device range stays unmapped). */
#define S2_PAGE_SIZE    0x1000ULL
#define S2_L2_BLOCK     0x200000ULL
#define S2_L1_BLOCK     0x40000000ULL

struct s2_pool {
    u64 *pages;      /* npages x 512 descriptors, 4 KB-aligned */
    u32  npages;
    u64  bitmap[(S2_POOL_PAGES + 63) / 64];   /* 1 = allocated */
};

/*
 * Attribute selectors for s2_map()/s2_protect(). The number picks the
 * (MemAttr, shareability) pair the encoder installs; it is NOT a MAIR index —
 * stage-2 block/page descriptors carry their own MemAttr (ARM DDI0487
 * D8.2.6), MAIR_EL2 only ever applies to EL2's stage 1.
 */
#define S2_ATTR_MASK          0x3U
#define S2_ATTR_NORMAL_WB     0x0U   /* Normal, WB RW-allocate, Inner Shareable */
#define S2_ATTR_DEVICE_nGnRE  0x1U   /* Device-nGnRE, Outer Shareable          */

/* Permissions (stage-2 S2AP) and execute-never, OR-ed into the same word. */
#define S2_PROT_MASK  (0x3U << 2)
#define S2_PROT_RO    (0x1U << 2)    /* read-only: the write bit stays 0 */
#define S2_PROT_RW    (0x3U << 2)
#define S2_XN         (0x1U << 4)    /* Device mappings are always XN */

/* Prepare `mem` (npages x 4 KB, 4 KB-aligned) as a table-page pool. */
int   s2_pool_init(struct s2_pool *p, void *mem, u32 npages);
/* One zeroed table page, or NULL when the pool is exhausted. */
u64  *s2_pool_alloc(struct s2_pool *p);
void  s2_pool_free(struct s2_pool *p, u64 *table);

/*
 * Map ipa..ipa+len to pa..pa+len with `attr`, creating tables as needed.
 * len must be a multiple of 4 KB; the largest block that fits both alignments
 * is used (1 GB, then 2 MB, then 4 KB pages). Ranges must be unmapped: an
 * existing entry in the way is S2_EINVAL, as is any request that would have
 * to split an existing block or page (no split support in this slice).
 * *flush is set when a descriptor changed.
 */
int s2_map(u64 *root, struct s2_pool *p, u64 ipa, u64 pa, u64 len, u32 attr,
           bool *flush);

/* Unmap a range that is mapped exactly as asked at block/page granularity;
 * table pages left empty are returned to the pool. *flush as in s2_map. */
int s2_unmap(u64 *root, struct s2_pool *p, u64 ipa, u64 len, bool *flush);

/* Change attributes/permissions (S2AP, XN, MemAttr) of an already mapped
 * range. Output addresses are preserved. *flush is clear when nothing
 * changed. */
int s2_protect(u64 *root, struct s2_pool *p, u64 ipa, u64 len, u32 attr,
               bool *flush);

#endif /* HV_S2_TABLE_H */
