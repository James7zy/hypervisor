/* SPDX-License-Identifier: TBD */
#include <vcpu_state.h>

enum vcpu_state vcpu_state_stop(enum vcpu_state *s)
{
    /* The returned state and OFF publication share one linearization point:
     * a sweep must kick if LOAD won the race, not use an earlier sample. */
    return __atomic_exchange_n(s, VCPU_OFF, __ATOMIC_ACQ_REL);
}

int vcpu_state_step(enum vcpu_state *s, enum vcpu_event e)
{
    enum vcpu_state from, to;

    switch (e) {
    case VCPU_EV_CPU_ON:
        from = VCPU_OFF;
        to = VCPU_ON_PENDING;
        break;
    case VCPU_EV_PICK:
        from = VCPU_ON_PENDING;
        to = VCPU_RUNNABLE;
        break;
    case VCPU_EV_LOAD:
        from = VCPU_RUNNABLE;
        to = VCPU_RUNNING;
        break;
    case VCPU_EV_BLOCK:
        from = VCPU_RUNNING;
        to = VCPU_BLOCKED;
        break;
    case VCPU_EV_WAKE:
        from = VCPU_BLOCKED;
        to = VCPU_RUNNABLE;
        break;
    case VCPU_EV_YIELD:
        from = VCPU_RUNNING;
        to = VCPU_RUNNABLE;
        break;
    case VCPU_EV_OFF:
        return vcpu_state_stop(s) == VCPU_OFF ? -1 : 0;
    default:
        return -1;
    }
    /* A stale read must never resurrect a vCPU stopped by another pCPU. */
    return __atomic_compare_exchange_n(s, &from, to, false,
                                      __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ? 0 : -1;
}
