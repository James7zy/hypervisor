/* SPDX-License-Identifier: TBD */
#include <mmio.h>
#include "data_abort.h"

/* ESR_EL2 ISS fields for a Data Abort (EC = 0x24). */
#define ISS_ISV(iss)  (((iss) >> 24) & 0x1U)   /* Instruction Syndrome Valid */
#define ISS_SAS(iss)  (((iss) >> 22) & 0x3U)   /* Access size 00..11 -> 1/2/4/8 */
#define ISS_SRT(iss)  (((iss) >> 16) & 0x1FU)  /* Transfer register 0..31 */
#define ISS_WNR(iss)  (((iss) >> 6)  & 0x1U)   /* Write not Read */

/* SAS (00,01,10,11) -> access size in bytes (1,2,4,8). */
static u8 mmio_sas_to_bytes(u32 sas)
{
    return (u8)(1U << sas);
}

struct abort_info abort_classify(const struct abort_snapshot *fault)
{
    u32 iss = (u32)fault->esr;
    struct abort_info info = {
        .ec = (u32)(fault->esr >> 26) & 0x3fU,
        .fsc = iss & 0x3fU,
        .class_name = "unknown",
    };
    bool translation = info.fsc >= 0x05 && info.fsc <= 0x07;
    bool access_flag = info.fsc >= 0x09 && info.fsc <= 0x0b;
    bool permission = info.fsc >= 0x0d && info.fsc <= 0x0f;
    bool s1ptw = (iss & (1U << 7)) != 0;
    bool cm = (iss & (1U << 8)) != 0;

    if (info.ec != 0x24 && info.ec != 0x20)
        return info;
    if (translation)
        info.class_name = "translation";
    else if (access_flag)
        info.class_name = "access-flag";
    else if (permission)
        info.class_name = "permission";
    /* FnV is defined for synchronous external abort (not a universal flag). */
    info.far_valid = translation || access_flag || permission ||
                     (info.fsc == 0x10 && !(iss & (1U << 10)));
    info.wnr_valid = info.ec == 0x24 && (translation || access_flag || permission);
    if ((translation || access_flag) && !s1ptw) {
        /* Non-LPA2 FIPA: HPFAR[39:4] -> IPA[47:12], then profile bounds. */
        info.ipa = ((fault->hpfar & 0xfffffffff0ULL) << 8) |
                   (fault->far & 0xfffULL);
        info.ipa_valid = (fault->hpfar >> 40) == 0 && info.ipa < (1ULL << 39);
    }
    info.mmio_allowed = info.ec == 0x24 && translation && ISS_ISV(iss) &&
                        !s1ptw && !cm && info.ipa_valid;
    return info;
}

int mmio_handle_data_abort(struct arch_regs *regs, const struct abort_snapshot *fault)
{
    u32 iss = (u32)fault->esr;
    struct abort_info info = abort_classify(fault);

    if (!info.mmio_allowed)
        return -1;

    u32 srt = ISS_SRT(iss);

    struct mmio_access acc;
    acc.addr     = info.ipa;
    acc.offset   = 0;   /* filled in by mmio_bus_dispatch */
    acc.size     = mmio_sas_to_bytes(ISS_SAS(iss));
    acc.is_write = (ISS_WNR(iss) != 0U);
    acc.data     = 0;

    if (acc.is_write) {
        /* Source GPR -> data. SRT=31 is the zero register. */
        acc.data = (srt == 31U) ? 0ULL : regs->x[srt];
    }

    /* No region, or the device failed: caller logs DFSC + parks. */
    if (mmio_bus_dispatch(&acc) != 0)
        return -1;

    if (!acc.is_write && srt != 31U) {
        /* Load: device result -> destination GPR. */
        regs->x[srt] = acc.data;
    }

    /* Advance past the faulting load/store; el1_sync_handler erets back. */
    regs->elr_el2 += 4;
    return 0;
}

