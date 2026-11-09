/* SPDX-License-Identifier: TBD */
#ifndef HV_ARCH_SYSREG_H
#define HV_ARCH_SYSREG_H

#include <types.h>

#define HCR_EL2_VM   (1ULL << 0)
#define HCR_EL2_FMO  (1ULL << 3)
#define HCR_EL2_IMO  (1ULL << 4)
#define HCR_EL2_AMO  (1ULL << 5)
#define HCR_EL2_TWI  (1ULL << 13)
#define HCR_EL2_TWE  (1ULL << 14)
#define HCR_EL2_TID3 (1ULL << 18)
#define HCR_EL2_TSC  (1ULL << 19)
#define HCR_EL2_RW   (1ULL << 31)

/* Non-VHE host idle routes physical IRQs before the first guest entry. */
#define HCR_EL2_HOST_BASE (HCR_EL2_RW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO)
#define HCR_EL2_GUEST_BASE (HCR_EL2_HOST_BASE | HCR_EL2_VM | HCR_EL2_TWI | \
                            HCR_EL2_TWE | HCR_EL2_TID3 | HCR_EL2_TSC)
_Static_assert(HCR_EL2_HOST_BASE == 0x80000038ULL, "host HCR baseline");
_Static_assert(HCR_EL2_GUEST_BASE == 0x800c6039ULL, "guest HCR unchanged");

#define SYSREG_READ(reg) ({                              \
    u64 __v;                                             \
    __asm__ volatile("mrs %0, " #reg : "=r"(__v));       \
    __v;                                                 \
})

#define SYSREG_WRITE(reg, val) do {                      \
    u64 __v = (u64)(val);                                \
    __asm__ volatile("msr " #reg ", %0" :: "r"(__v));    \
} while (0)

#endif /* HV_ARCH_SYSREG_H */
