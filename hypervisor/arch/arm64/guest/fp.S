/* SPDX-License-Identifier: TBD */
/*
 * Guest FP/SIMD context save/restore (M11 C T3, ADR-0020: eager). The
 * hypervisor itself is built -mgeneral-regs-only and never touches v0-v31,
 * so the live FP file is always the current vCPU's. Layout pinned in
 * tests/check_offsets_target.c: q[0..31] at 0..511, fpsr 512, fpcr 520.
 * FPSR/FPCR use str/ldr: stp's signed offset tops out at 504.
 */
    .arch   armv8-a+fp+simd
    .text

/* void fp_save(struct fp_state *x0) */
    .globl  fp_save
fp_save:
    stp     q0,  q1,  [x0, #0]
    stp     q2,  q3,  [x0, #32]
    stp     q4,  q5,  [x0, #64]
    stp     q6,  q7,  [x0, #96]
    stp     q8,  q9,  [x0, #128]
    stp     q10, q11, [x0, #160]
    stp     q12, q13, [x0, #192]
    stp     q14, q15, [x0, #224]
    stp     q16, q17, [x0, #256]
    stp     q18, q19, [x0, #288]
    stp     q20, q21, [x0, #320]
    stp     q22, q23, [x0, #352]
    stp     q24, q25, [x0, #384]
    stp     q26, q27, [x0, #416]
    stp     q28, q29, [x0, #448]
    stp     q30, q31, [x0, #480]
    mrs     x1, fpsr
    str     x1, [x0, #512]
    mrs     x1, fpcr
    str     x1, [x0, #520]
    ret

/* void fp_restore(const struct fp_state *x0) */
    .globl  fp_restore
fp_restore:
    ldp     q0,  q1,  [x0, #0]
    ldp     q2,  q3,  [x0, #32]
    ldp     q4,  q5,  [x0, #64]
    ldp     q6,  q7,  [x0, #96]
    ldp     q8,  q9,  [x0, #128]
    ldp     q10, q11, [x0, #160]
    ldp     q12, q13, [x0, #192]
    ldp     q14, q15, [x0, #224]
    ldp     q16, q17, [x0, #256]
    ldp     q18, q19, [x0, #288]
    ldp     q20, q21, [x0, #320]
    ldp     q22, q23, [x0, #352]
    ldp     q24, q25, [x0, #384]
    ldp     q26, q27, [x0, #416]
    ldp     q28, q29, [x0, #448]
    ldp     q30, q31, [x0, #480]
    ldr     x1, [x0, #512]
    msr     fpsr, x1
    ldr     x1, [x0, #520]
    msr     fpcr, x1
    ret
