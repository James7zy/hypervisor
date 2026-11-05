/* SPDX-License-Identifier: TBD */
#ifndef SVM_FP_H
#define SVM_FP_H

/* FP/SIMD canaries (M11 C T3). Built with -mgeneral-regs-only, so the
 * compiler never allocates v0-v31 or touches FPCR/FPSR: whatever these
 * stamps leave in the FP file must still be there after any number of
 * switches, iff the hypervisor saves/restores the FP context. Needs EL1 FP
 * enabled first (CPACR stamp FPEN=01). Lane value = seed + reg index. */
static inline void fp_stamp(unsigned long seed, unsigned long fpcr, unsigned long fpsr)
{
    __asm__ volatile(
        ".irp i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n\t"
        "add x9, %0, #\\i\n\t"
        "dup v\\i\\().2d, x9\n\t"
        ".endr\n\t"
        "msr fpcr, %1\n\t"
        "msr fpsr, %2"
        :: "r"(seed), "r"(fpcr), "r"(fpsr) : "x9", "memory");
}

/* Mismatching lanes + control registers; 0 == intact. */
static inline unsigned long fp_check(unsigned long seed, unsigned long fpcr,
                              unsigned long fpsr)
{
    unsigned long bad;
    __asm__ volatile(
        "mov x13, #0\n\t"
        ".irp i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n\t"
        "add x12, %1, #\\i\n\t"
        "mov x10, v\\i\\().d[0]\n\t"
        "mov x11, v\\i\\().d[1]\n\t"
        "cmp x10, x12\n\t"
        "cinc x13, x13, ne\n\t"
        "cmp x11, x12\n\t"
        "cinc x13, x13, ne\n\t"
        ".endr\n\t"
        "mrs x10, fpcr\n\t"
        "cmp x10, %2\n\t"
        "cinc x13, x13, ne\n\t"
        "mrs x10, fpsr\n\t"
        "cmp x10, %3\n\t"
        "cinc x13, x13, ne\n\t"
        "mov %0, x13"
        : "=&r"(bad) : "r"(seed), "r"(fpcr), "r"(fpsr)
        : "x10", "x11", "x12", "x13", "cc");
    return bad;
}


/* Nonzero lanes + control registers; 0 == the whole FP file reads zero
 * (fresh-load leak canary, M11 C T5). */
static inline unsigned long fp_check_zero(void)
{
    unsigned long bad;
    __asm__ volatile(
        "mov x13, #0\n\t"
        ".irp i,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n\t"
        "mov x10, v\\i\\().d[0]\n\t"
        "mov x11, v\\i\\().d[1]\n\t"
        "orr x10, x10, x11\n\t"
        "cmp x10, #0\n\t"
        "cinc x13, x13, ne\n\t"
        ".endr\n\t"
        "mrs x10, fpcr\n\t"
        "cmp x10, #0\n\t"
        "cinc x13, x13, ne\n\t"
        "mrs x10, fpsr\n\t"
        "cmp x10, #0\n\t"
        "cinc x13, x13, ne\n\t"
        "mov %0, x13"
        : "=&r"(bad) :: "x10", "x11", "x13", "cc");
    return bad;
}

#endif /* SVM_FP_H */
