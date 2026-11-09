/* SPDX-License-Identifier: TBD */
/*
 * HV-005 host test: generic hypercall dispatch and the derived per-VM state it
 * shares with the EL2 shell's vm_list. Linked against the real hypercall.c and
 * vm.c with no hardware: a caller is a struct vm pointer, which is the point --
 * a guest cannot name itself in an argument register (E2).
 */
#include <hypercall.h>
#include <vm.h>
#include <vm_config.h>
#include <percpu.h>
#include <sched.h>
#include "host_test.h"

int tests_run;
int tests_failed;

/* Three VMs: 0 is the Service VM (E3), 1 and 2 are ordinary guests. */
const struct vm_config vm_configs[NR_VMS];

static void author_vms(void)
{
    for (u32 i = 0; i < (u32)NR_VMS; i++) {
        vm[i] = (struct vm){ .id = i, .config = &vm_configs[i] };
        for (u32 j = 0; j < (u32)VCPUS_PER_VM; j++) {
            vm[i].vcpu[j].owner    = &vm[i];
            vm[i].vcpu[j].vcpu_idx = j;
            vm[i].vcpu[j].state    = VCPU_OFF;
        }
        /* As vm_init authors the boot vCPU; the sibling stays OFF until the
         * guest's own PSCI CPU_ON. */
        vm[i].vcpu[0].state = VCPU_RUNNABLE;
    }
}

/* hypercall.c pauses/resumes through these two; sched.c cannot be linked here
 * (it would need a real pCPU's queue and arch hooks). The dispatch contract
 * under test is only which code comes back, so the flag flip -- the real
 * vm_pause/vm_resume -- is all this stands in for. */
int sched_vm_pause(struct vm *v) { return vm_pause(v); }
int sched_vm_resume(struct vm *v) { return vm_resume(v); }

static void test_ids(void)
{
    /* Shipped ids keep their value (E6): the SVM scenarios hardcode this one. */
    TEST_ASSERT(HC_GUEST_DONE == 0x87000001UL);
    TEST_ASSERT(HC_VERSION == 0x87000000UL);
    TEST_ASSERT(HC_VM_QUERY == 0x87000100UL);
    TEST_ASSERT(HC_VM_PAUSE == 0x87000101UL);
    TEST_ASSERT(HC_VM_RESUME == 0x87000102UL);
    TEST_ASSERT(HC_ID(HC_MOD_VM, 1) == 0x87000101UL);
    TEST_ASSERT(HC_EBUSY == -5);
}

static void test_derived_state(void)
{
    struct vm *guest = &vm[1];

    TEST_ASSERT(vm_is_service(&vm[0]));
    TEST_ASSERT(!vm_is_service(guest));
    TEST_ASSERT(vm_online_vcpus(guest) == 1);
    TEST_ASSERT(vm_any_vcpu_on(guest));

    guest->vcpu[1].state = VCPU_BLOCKED;   /* a blocked vCPU is still online */
    TEST_ASSERT(vm_online_vcpus(guest) == 2);
    TEST_ASSERT(vm_any_vcpu_on(&vm[2]));

    vm[2].vcpu[0].state = VCPU_OFF;
    TEST_ASSERT(vm_online_vcpus(&vm[2]) == 0);
    TEST_ASSERT(!vm_any_vcpu_on(&vm[2]));
}

static void test_version(void)
{
    u64 ret = 0xDEADBEEFULL;

    /* Any VM, no arguments used. */
    TEST_ASSERT(hypercall_dispatch(&vm[1], HC_VERSION, 0, 0, &ret) == HC_OK);
    TEST_ASSERT(ret == 0x00000002ULL);   /* major<<16 | minor = 0.2 */
}

static void test_vm_query(void)
{
    struct vm *service = &vm[0];
    struct vm *guest   = &vm[1];
    u64 ret;

    /* Self query: succeeds for anyone, reports the caller's own word. */
    ret = 0;
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_QUERY, 1, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & 0xFFFFULL) == 1);          /* bits[15:0]  = vmid */
    TEST_ASSERT(((ret >> 16) & 1ULL) == 0);       /* bit16       = is-service */
    TEST_ASSERT((ret >> 17) == 2);                /* bits[31:17] = online vCPUs */

    /* Querying someone else: a plain guest is refused no matter what it puts
     * in x1, and a refused call writes nothing back. */
    ret = 0;
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_QUERY, 0, 0, &ret) == HC_EPERM);
    TEST_ASSERT(ret == 0);

    /* The Service VM may query others; the word describes the target. */
    ret = 0;
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, 1, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & 0xFFFFULL) == 1);
    TEST_ASSERT(((ret >> 16) & 1ULL) == 0);
    TEST_ASSERT((ret >> 17) == 2);

    ret = 0;
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, 0, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & 0xFFFFULL) == 0);
    TEST_ASSERT(((ret >> 16) & 1ULL) == 1);

    /* A VM whose vCPUs are all OFF still answers: vmid, role, count 0. */
    vm[2].vcpu[0].state = VCPU_OFF;
    ret = 0;
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, 2, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & 0xFFFFULL) == 2);
    TEST_ASSERT((ret >> 17) == 0);

    /* No such VM: out of range, and the id field's width bound. */
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, (u64)NR_VMS, 0, &ret)
                == HC_ENODEV);
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_QUERY, 0x1FFFFULL, 0, &ret)
                == HC_ENODEV);
}

static void test_vm_pause_resume(void)
{
    struct vm *service = &vm[0];
    struct vm *guest   = &vm[1];
    u64 ret = 0;

    /* Service VM only (F4), so a guest is refused before its arguments are
     * looked at -- even when it names itself. */
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_PAUSE, 1, 0, &ret) == HC_EPERM);
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_RESUME, 1, 0, &ret) == HC_EPERM);
    TEST_ASSERT(hypercall_dispatch(guest, HC_VM_PAUSE, 0, 0, &ret) == HC_EPERM);

    /* Itself: EINVAL. */
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_PAUSE, 0, 0, &ret) == HC_EINVAL);
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_RESUME, 0, 0, &ret) == HC_EINVAL);

    /* No such VM, and the vmid field's width bound. */
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_PAUSE, (u64)NR_VMS, 0, &ret)
                == HC_ENODEV);
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_PAUSE, 0x1FFFFULL, 0, &ret)
                == HC_ENODEV);

    /* Pause, read it back out of the query word, pause again: EBUSY. */
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_PAUSE, 1, 0, &ret) == HC_OK);
    TEST_ASSERT(vm_is_paused(guest));
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, 1, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & HC_VM_QUERY_PAUSED) != 0);       /* bit32 = paused (F5) */
    TEST_ASSERT((ret & 0xFFFFULL) == 1);          /* lower fields untouched */
    TEST_ASSERT(((ret >> 17) & 0x7FFFULL) == 2);
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_PAUSE, 1, 0, &ret) == HC_EBUSY);

    /* Resume clears it, and a second resume is EBUSY too. */
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_RESUME, 1, 0, &ret) == HC_OK);
    TEST_ASSERT(!vm_is_paused(guest));
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_QUERY, 1, 0, &ret) == HC_OK);
    TEST_ASSERT((ret & HC_VM_QUERY_PAUSED) == 0);
    TEST_ASSERT(hypercall_dispatch(service, HC_VM_RESUME, 1, 0, &ret) == HC_EBUSY);
}

static void test_unknown(void)
{
    u64 ret = 0;

    TEST_ASSERT(hypercall_dispatch(&vm[0], 0x8700FF00U, 0, 0, &ret) == HC_ENOSYS);
    TEST_ASSERT(hypercall_dispatch(&vm[1], 0x8700FF00U, 0, 0, &ret) == HC_ENOSYS);
    /* HC_GUEST_DONE never returns, so it is not dispatchable: vmexit.c keeps
     * it before the table (E1). */
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_GUEST_DONE, 0, 0, &ret) == HC_ENOSYS);
}

int main(void)
{
    test_ids();
    author_vms();
    test_derived_state();
    test_version();
    test_vm_query();
    test_vm_pause_resume();
    test_unknown();

    printf("hypercall %s (%d assertions)\n",
           tests_failed ? "FAIL" : "PASS", tests_run);
    return tests_failed != 0;
}
