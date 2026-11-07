/* SPDX-License-Identifier: TBD */
/* The real generic loop, with x86's existing host-safe layout/lock hooks. */
#include <setjmp.h>
#include <sched.h>
#include <cpu.h>
#include <vm_config.h>
#include "host_test.h"

int tests_run;
int tests_failed;
struct vm vm[NR_VMS];
struct percpu percpu[NR_CPUS];
struct percpu *x86_this_percpu = &percpu[0];
static const struct vm_config config = { .vcpu_pcpu = {0, 0} };
static struct vcpu *resident;
static jmp_buf finished;
static int run_count;
static int resume_a_count;
/* RUN_YIELD: A gives up the pCPU from inside its own run (WFI trap path).
 * RUN_PREEMPT: the quantum tick unwinds A via hv_restore, so run() simply
 * returns with A still RUNNING and the loop must do the rest. */
static bool preempt_mode;

void cpu_arch_irq_enable(void) {}
void cpu_arch_irq_disable(void) {}
void cpu_arch_kick(u32 cpu) { (void)cpu; }
void cpu_arch_idle(void)
{
    TEST_ASSERT(0);  /* A must not be stranded. */
    longjmp(finished, 1);
}

void vcpu_arch_put(struct vcpu *v)
{
    TEST_ASSERT(v == resident);  /* Never save B's hardware into A. */
    /* Real switch-away only ever leaves a RUNNABLE vCPU (RUNNING is out of
     * the queue, BLOCKED/OFF are not ours to park on the hardware). */
    TEST_ASSERT(__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VCPU_RUNNABLE);
}

static void switch_in(struct vcpu *v)
{
    resident = v;
    if (v == &vm[1].vcpu[0]) {
        /* Deterministic remote SYSTEM_OFF after hardware load but before
         * sched_loop's LOAD event. This must invalidate the old prev. */
        TEST_ASSERT(vcpu_state_step(&v->state, VCPU_EV_OFF) == 0);
    } else {
        resume_a_count++;
    }
}

void vcpu_arch_load(struct vcpu *v) { switch_in(v); }
void vcpu_arch_resume(struct vcpu *v) { switch_in(v); }

void vcpu_arch_run(struct vcpu *v)
{
    TEST_ASSERT(v == &vm[0].vcpu[0]);
    TEST_ASSERT(v == resident);
    if (++run_count == 2)
        longjmp(finished, 1);
    if (preempt_mode) {
        /* The tick handler: flag request + unwind, no state change here. */
        sched_preempt_request();
        return;
    }
    TEST_ASSERT(vcpu_state_step(&v->state, VCPU_EV_YIELD) == 0);
    sched_yield_cur();  /* B was already queued, A follows it. */
}

/* Slice D: a preemption request must cost the running vCPU its turn — the
 * loop converts it RUNNING->RUNNABLE, queues it, and B runs before A is
 * picked again. A returning to run() at all (run_count reaches 2 without
 * the idle assert firing) is the no-starvation half. */
static void preempt_case(void)
{
    for (u32 i = 0; i < NR_VMS; i++) {
        vm[i] = (struct vm){ .id = i, .config = &config };
        vm[i].vcpu[0].owner = &vm[i];
        vm[i].vcpu[0].state = VCPU_RUNNABLE;
        vm[i].vcpu[0].launched = true;
    }
    resident = NULL;
    run_count = resume_a_count = 0;
    percpu[0].cur_vcpu = NULL;
    preempt_mode = true;
    sched_init();
    if (!setjmp(finished))
        sched_loop(0);
    TEST_ASSERT(run_count == 2);
    /* Two resume paths for A means a switch-away (to B) happened between the
     * request and A's re-pick: the fast path would have re-entered A
     * directly, leaving this at 1. */
    TEST_ASSERT(resume_a_count == 2);
}

int main(void)
{
    for (volatile int launched = 0; launched <= 1; launched++) {
        for (u32 i = 0; i < NR_VMS; i++) {
            vm[i] = (struct vm){ .id = i, .config = &config };
            vm[i].vcpu[0].owner = &vm[i];
            vm[i].vcpu[0].state = VCPU_RUNNABLE;
            vm[i].vcpu[0].launched = (i == 0) || launched;
        }
        resident = NULL;
        run_count = resume_a_count = 0;
        percpu[0].cur_vcpu = NULL;
        sched_init();
        if (!setjmp(finished))
            sched_loop(0);
        TEST_ASSERT(run_count == 2);
        TEST_ASSERT(resume_a_count == 2);
        TEST_ASSERT(vm[1].vcpu[0].state == VCPU_OFF);
    }
    preempt_case();
    printf("scheduler %s (%d assertions)\n", tests_failed ? "FAIL" : "PASS", tests_run);
    return tests_failed != 0;
}
