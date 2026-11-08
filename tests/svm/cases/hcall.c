/* SPDX-License-Identifier: TBD */
/*
 * SVM case "hcall" (HV-005): the hypercall ABI seen from the guest side.
 *
 * ONE binary is loaded for both VMs of the svm_dual profile (SVM_BIN and
 * SVM_BIN2 point at the same .bin), so nothing here may be baked in per VM:
 * a guest cannot name itself (E2), and the only identity fact on offer is the
 * answer set of HC_VM_QUERY -- a plain guest may query exactly one vmid (its
 * own), the Service VM may query every existing one.
 *
 * Asserted per VM: HC_VERSION answers 0.1; the self query reports
 * vmid/service/online as that VM's own; querying the other VM succeeds for
 * the Service VM and is refused with HC_EPERM for the guest; an unused id in
 * the vendor range answers HC_ENOSYS. The verdict rides back in the
 * HC_GUEST_DONE payload ("SVM HVC: done (x1=0x...)"), so a silent mismatch
 * fails the expect file even if every printed line happens to appear.
 *
 * Run: make test-qemu-hcall   (included in the `test` aggregate)
 */
#include "../svm_lib.h"

/* include/hypercall.h: SMCCC vendor range, base + (module << 8) + index. */
#define HC_VERSION   0x87000000UL
#define HC_VM_QUERY  0x87000100UL
#define HC_UNUSED    0x8700FF00UL   /* no call has this id */

/* Return convention: x0 = 0 or a negative errno. */
#define HC_EPERM     (-2L)
#define HC_ENOSYS    (-3L)
#define HC_ENODEV    (-4L)

/* One past any vmid these profiles configure: the discovery probe range. */
#define PROBE_VMS 4

static int fails;

static unsigned long now(void)
{
    unsigned long c;

    __asm__ volatile("isb\n\tmrs %0, CNTVCT_EL0" : "=r"(c) :: "memory");
    return c;
}

/*
 * Issue one hypercall and keep BOTH halves of the answer: x0 is the return
 * code, x1 is the ABI's payload word (svm_hvc() drops x1, and every check
 * below needs it).
 */
static unsigned long hvc2(unsigned long id, unsigned long a1, unsigned long *x1)
{
    register unsigned long r0 __asm__("x0") = id;
    register unsigned long r1 __asm__("x1") = a1;

    __asm__ volatile("hvc #0" : "+r"(r0), "+r"(r1) :: "memory");
    *x1 = r1;
    return r0;
}

static void put_dec(unsigned long v)
{
    char buf[21];
    unsigned int i = sizeof(buf) - 1U;

    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + (unsigned int)(v % 10UL));
        v /= 10UL;
    } while (v != 0UL);
    svm_puts(&buf[i]);
}

static void put_sdec(long v)
{
    if (v < 0L) {
        svm_puts("-");
        put_dec((unsigned long)-v);
        return;
    }
    put_dec((unsigned long)v);
}

/* Marker prefix: "HCALL: vm<self> " so each VM's lines are its own. */
static void mark(unsigned long self){
    svm_puts("HCALL: vm");
    put_dec(self);
    svm_puts(" ");
}

static void expect(int ok, const char *what)
{
    if (ok)
        return;
    fails++;
    svm_puts("HCALL: FAIL ");
    svm_puts(what);
    svm_puts("\n");
}

/*
 * Guest output is byte-at-a-time, so two VMs printing at once interleave
 * mid-line and no expect line would match; a hypervisor printk in the middle
 * splits a guest line the same way. Stagger the printers: the pCPU boot
 * announcements are over within a few ms, the Service VM prints after them and
 * the guest well after that (both wait from guest entry, on separate pCPUs).
 */
static void wait_ms(unsigned long n)
{
    unsigned long f;
    unsigned long until;

    __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(f));
    f /= 1000UL;
    until = now() + n * f;
    while (now() < until)
        ;
}

void SVM_ENTRY _start(void)
{
    unsigned long x1 = 0;
    unsigned long self = 0;
    unsigned long peer;
    int seen_self = 0;
    int seen_denied = 0;
    int is_service;
    long rc;
    int probe;

    /*
     * Who am I? Probe every configured vmid: a refusal (HC_EPERM) proves the
     * caller is not the Service VM, an answer (and the end of the range at
     * HC_ENODEV) proves which ids the caller is allowed to see. A guest's
     * answer set is exactly itself; the Service VM's is everything, and E3
     * puts it at vmid 0.
     */
    for (probe = 0; probe < PROBE_VMS; probe++) {
        rc = (long)hvc2(HC_VM_QUERY, (unsigned long)probe, &x1);
        if (rc == HC_ENODEV)
            break;
        if (rc == HC_EPERM) {
            seen_denied = 1;
            continue;
        }
        expect(rc == 0L, "probe rc");
        if (rc == 0L) {
            expect((x1 & 0xFFFFUL) == (unsigned long)probe, "probe vmid");
            if (!seen_self) {
                seen_self = 1;
                self = (unsigned long)probe;
            }
        }
    }
    is_service = !seen_denied;
    expect(seen_self, "no self");
    expect(!is_service || self == 0UL, "service vmid");

    wait_ms(is_service ? 100UL : 800UL);

    /* HC_VERSION: any VM, no arguments, x1 = major<<16 | minor. */
    mark(self);
    svm_puts("version ");
    rc = (long)hvc2(HC_VERSION, 0UL, &x1);
    if (rc == 0L) {
        put_dec((x1 >> 16) & 0xFFFFUL);
        svm_puts(".");
        put_dec(x1 & 0xFFFFUL);
        svm_puts("\n");
    } else {
        svm_puts("rc=");
        put_sdec(rc);
        svm_puts("\n");
    }
    expect(rc == 0L, "version rc");
    expect((x1 >> 16) == 0UL, "version major");

    /* Own word: vmid | is-service << 16 | online vCPUs << 17. */
    mark(self);
    rc = (long)hvc2(HC_VM_QUERY, self, &x1);
    svm_puts("self vmid=");
    put_dec(x1 & 0xFFFFUL);
    svm_puts(" service=");
    put_dec((x1 >> 16) & 1UL);
    svm_puts(" online=");
    put_dec(x1 >> 17);
    svm_puts("\n");
    expect(rc == 0L, "self rc");
    expect((x1 & 0xFFFFUL) == self, "self vmid");
    expect(((x1 >> 16) & 1UL) == (is_service ? 1UL : 0UL), "self service bit");
    expect((x1 >> 17) >= 1UL, "self online");

    /* The other VM: allowed for the Service VM, refused for a guest, and the
     * refusal must not depend on anything the guest puts in x1. */
    peer = (self == 0UL) ? 1UL : 0UL;
    mark(self);
    svm_puts("query peer rc=");
    rc = (long)hvc2(HC_VM_QUERY, peer, &x1);
    put_sdec(rc);
    svm_puts("\n");
    expect(rc == (is_service ? 0L : HC_EPERM), "peer rc");
    if (rc == 0L)
        expect((x1 & 0xFFFFUL) == peer, "peer vmid");

    /* An id in the vendor range that no call implements. */
    mark(self);
    svm_puts("unused rc=");
    rc = (long)hvc2(HC_UNUSED, 0UL, &x1);
    put_sdec(rc);
    svm_puts("\n");
    expect(rc == HC_ENOSYS, "unused rc");

    /* The verdict is checked as text, so a surviving mismatch cannot hide:
     * expect lines are (x1=0x0) for VM0 and (x1=0x100) for VM1. */
    svm_done(0x100UL * self + (unsigned long)fails);
}
