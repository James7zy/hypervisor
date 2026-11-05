/* SPDX-License-Identifier: TBD */
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>
#include <vcpu_state.h>

#define ROUNDS 500000U
static const enum vcpu_state from[] = {
    VCPU_ON_PENDING, VCPU_RUNNABLE, VCPU_RUNNING, VCPU_BLOCKED, VCPU_RUNNING,
};
static const enum vcpu_event event[] = {
    VCPU_EV_PICK, VCPU_EV_LOAD, VCPU_EV_BLOCK, VCPU_EV_WAKE, VCPU_EV_YIELD,
};
static enum vcpu_state state;
static atomic_uint phase, ready, done;

static int contender(void *arg)
{
    int stopper = *(int *)arg;
    for (unsigned i = 1; i <= ROUNDS; i++) {
        while (atomic_load_explicit(&phase, memory_order_acquire) != i)
            ;
        atomic_fetch_add_explicit(&ready, 1, memory_order_acq_rel);
        while (atomic_load_explicit(&ready, memory_order_acquire) != 2)
            ;
        (void)vcpu_state_step(&state, stopper ? VCPU_EV_OFF : event[(i - 1) % 5]);
        atomic_fetch_add_explicit(&done, 1, memory_order_acq_rel);
    }
    return 0;
}

int main(void)
{
    thrd_t threads[2];
    int stop[2] = {0, 1};
    unsigned lost[5] = {0};
    for (int i = 0; i < 2; i++) {
        if (thrd_create(&threads[i], contender, &stop[i]) != thrd_success)
            return 2;
    }
    for (unsigned i = 1; i <= ROUNDS; i++) {
        __atomic_store_n(&state, from[(i - 1) % 5], __ATOMIC_RELAXED);
        atomic_store_explicit(&ready, 0, memory_order_relaxed);
        atomic_store_explicit(&done, 0, memory_order_relaxed);
        atomic_store_explicit(&phase, i, memory_order_release);
        while (atomic_load_explicit(&done, memory_order_acquire) != 2)
            ;
        if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) != VCPU_OFF)
            lost[(i - 1) % 5]++;
    }
    for (int i = 0; i < 2; i++)
        thrd_join(threads[i], NULL);
    unsigned total = 0;
    for (int i = 0; i < 5; i++) {
        printf("event %d: %u lost OFF transitions\n", event[i], lost[i]);
        total += lost[i];
    }
    printf("state race %s (%u rounds)\n", total ? "FAIL" : "PASS", ROUNDS);
    return total != 0;
}
