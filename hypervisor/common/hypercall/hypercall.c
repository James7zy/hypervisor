/* SPDX-License-Identifier: TBD */
/*
 * HV-005: hypercall dispatch. One table, one permission model, generic only
 * (no arch header) so the host tests build it and a second arch reuses it.
 */
#include <types.h>
#include <vm.h>
#include <sched.h>
#include <hypercall.h>

/* The ABI version this build implements. */
#define HC_VERSION_MAJOR 0U
#define HC_VERSION_MINOR 1U

/*
 * Caller classes a table entry can require. Every VM is HC_ROLE_ANY; the
 * Service VM additionally belongs to HC_ROLE_SERVICE (E3: VM0 is the Service
 * VM by static topology, so there is no role field in vm_config yet).
 */
#define HC_ROLE_ANY     0x1U
#define HC_ROLE_SERVICE 0x2U

struct hypercall {
    u32 id;
    u8  roles;   /* HC_ROLE_* classes allowed to call it */
    s64 (*fn)(struct vm *caller, u64 a1, u64 a2, u64 *ret);
};

/* The VM whose id is `vmid`, or NULL when no VM has that id. */
static struct vm *vm_by_id(u32 vmid)
{
    for (u32 i = 0; i < (u32)NR_VMS; i++)
        if (vm[i].id == vmid)
            return &vm[i];
    return NULL;
}

/*
 * x1 as the vmid the pause/resume pair acts on: the target VM, or NULL with
 * *err set. Targeting the caller is refused rather than allowed: a VM that
 * suspends itself never answers the call that did it.
 */
static struct vm *vm_arg_target(struct vm *caller, u64 a1, s64 *err)
{
    struct vm *target;

    if (a1 >> HC_VMID_BITS != 0U) {   /* same width rule as the query answer */
        *err = HC_ENODEV;
        return NULL;
    }
    target = vm_by_id((u32)a1);
    if (target == NULL) {
        *err = HC_ENODEV;
        return NULL;
    }
    if (target == caller) {
        *err = HC_EINVAL;
        return NULL;
    }
    *err = HC_OK;
    return target;
}

static s64 hc_version(struct vm *caller, u64 a1, u64 a2, u64 *ret)
{
    (void)caller;
    (void)a1;
    (void)a2;

    *ret = ((u64)HC_VERSION_MAJOR << 16) | (u64)HC_VERSION_MINOR;
    return HC_OK;
}

/*
 * State word: vmid | is-service << 16 | online vCPUs << 17 | paused << 32.
 * Derived state only: fields are added to this word, never moved.
 */
static s64 hc_vm_query(struct vm *caller, u64 a1, u64 a2, u64 *ret)
{
    struct vm *target;
    (void)a2;

    if (a1 >> HC_VMID_BITS != 0U)   /* vmid occupies bits[15:0] of the answer */
        return HC_ENODEV;
    target = vm_by_id((u32)a1);
    if (target == NULL)
        return HC_ENODEV;

    /* Argument-checked privilege, so it cannot live in the table's roles:
     * any VM may query itself, only the Service VM may query another. */
    if (target != caller && !vm_is_service(caller))
        return HC_EPERM;

    *ret = (u64)target->id
           | ((vm_is_service(target) ? 1ULL : 0ULL) << 16)
           | ((u64)vm_online_vcpus(target) << 17)
           | ((vm_is_paused(target) ? 1ULL : 0ULL) << 32);
    return HC_OK;
}

/*
 * Suspend/resume (HV-004 F2/F3). Both are Service-VM-only (table roles) and
 * asynchronous: the answer reports the state transition, not a stopped VM.
 * EBUSY means the target is already where the call wanted it.
 */
static s64 hc_vm_pause(struct vm *caller, u64 a1, u64 a2, u64 *ret)
{
    struct vm *target;
    s64 err;

    (void)a2;
    (void)ret;
    target = vm_arg_target(caller, a1, &err);
    if (target == NULL)
        return err;
    return sched_vm_pause(target) == 0 ? HC_OK : HC_EBUSY;
}

static s64 hc_vm_resume(struct vm *caller, u64 a1, u64 a2, u64 *ret)
{
    struct vm *target;
    s64 err;

    (void)a2;
    (void)ret;
    target = vm_arg_target(caller, a1, &err);
    if (target == NULL)
        return err;
    return sched_vm_resume(target) == 0 ? HC_OK : HC_EBUSY;
}

/* Ascending id order; dispatch is a linear scan over the few entries. */
static const struct hypercall hypercalls[] = {
    { HC_VERSION,   HC_ROLE_ANY,     hc_version   },
    { HC_VM_QUERY,  HC_ROLE_ANY,     hc_vm_query  },
    { HC_VM_PAUSE,  HC_ROLE_SERVICE, hc_vm_pause  },
    { HC_VM_RESUME, HC_ROLE_SERVICE, hc_vm_resume },
};

s64 hypercall_dispatch(struct vm *caller, u32 func_id, u64 a1, u64 a2, u64 *ret)
{
    u8 caller_roles = HC_ROLE_ANY
                      | (vm_is_service(caller) ? HC_ROLE_SERVICE : 0U);

    for (u32 i = 0; i < (u32)(sizeof(hypercalls) / sizeof(hypercalls[0])); i++) {
        if (hypercalls[i].id != func_id)
            continue;
        if ((hypercalls[i].roles & caller_roles) == 0U)
            return HC_EPERM;
        return hypercalls[i].fn(caller, a1, a2, ret);
    }
    return HC_ENOSYS;
}
