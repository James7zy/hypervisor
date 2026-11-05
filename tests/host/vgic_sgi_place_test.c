/* SPDX-License-Identifier: TBD */
#include <vgic_lr.h>
#include "host_test.h"

#define ST_MASK  (3ULL << 62)
#define ACTIVE   (2ULL << 62)
#define VID(lr)  ((unsigned)((lr) & ICH_LR_VINTID_MASK))

void test_vgic_sgi_place(void)
{
    u64 lr[2];

    /* Two free slots, three pending: lowest two placed, one left; the
     * occupied slots carry EOI so a deactivate signals the refill. */
    lr[0] = lr[1] = 0;
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0x7) == 0x4);
    TEST_ASSERT(VID(lr[0]) == 0 && VID(lr[1]) == 1);
    TEST_ASSERT((lr[0] & ST_MASK) == ICH_LR_STATE_PENDING);
    TEST_ASSERT((lr[0] & ICH_LR_EOI) && (lr[1] & ICH_LR_EOI));
    TEST_ASSERT((lr[0] & ICH_LR_GROUP1) && !(lr[0] & ICH_LR_HW));

    /* Guest deactivated slot 0 (now invalid, EOI still set): the leftover
     * goes there; nothing left, so EOI is cleared on both. */
    lr[0] &= ~ST_MASK;
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0x4) == 0);
    TEST_ASSERT(VID(lr[0]) == 2 && VID(lr[1]) == 1);
    TEST_ASSERT(!(lr[0] & ICH_LR_EOI) && !(lr[1] & ICH_LR_EOI));

    /* Coalesce: INTID already pending in a slot — no second LR. */
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0x2) == 0);
    TEST_ASSERT(VID(lr[0]) == 2 && VID(lr[1]) == 1);

    /* Active-only slot with the same INTID: becomes active+pending. */
    lr[1] = (lr[1] & ~ST_MASK) | ACTIVE;
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0x2) == 0);
    TEST_ASSERT((lr[1] & ST_MASK) == (ACTIVE | ICH_LR_STATE_PENDING));

    /* Both slots active-only with other INTIDs: nothing placed, EOI armed. */
    lr[0] = (lr[0] & ~ST_MASK) | ACTIVE;
    lr[1] = (lr[1] & ~ST_MASK) | ACTIVE;
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0x8) == 0x8);
    TEST_ASSERT((lr[0] & ICH_LR_EOI) && (lr[1] & ICH_LR_EOI));

    /* Nothing pending: an invalid slot with a stale EOI is zeroed (drops
     * the maintenance condition). */
    lr[0] = ICH_LR_EOI | 5U;
    lr[1] = 0;
    TEST_ASSERT(vgic_sgi_place_lrs(lr, 0) == 0);
    TEST_ASSERT(lr[0] == 0 && lr[1] == 0);
}
