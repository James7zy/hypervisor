/* SPDX-License-Identifier: TBD */
/* T1 behavioral RED: real sched/vm/hypercall code, only hardware is replaced. */
#include <setjmp.h>
#include <sched.h>
#include <cpu.h>
#include <hypercall.h>
#include <vm_config.h>
#include "host_test.h"

int tests_run;
int tests_failed;
struct percpu percpu[NR_CPUS];
struct percpu *x86_this_percpu = &percpu[0];
static const struct vm_config config = { .vcpu_pcpu = {0, 1} };
static jmp_buf finished;
static struct vcpu *resident;
static unsigned entries;
static unsigned kicks[NR_CPUS];
static bool pause_in_load;
/* Test the approved wire bit without introducing the T2 production API. */
#define EXPECT_EXEC_QUIESCED (1ULL << 33)

static u64 query(void)
{
    u64 ret = 0;
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_QUERY, 1, 0, &ret) == HC_OK);
    return ret;
}

static void pause_target(void)
{
    u64 ret = 0;
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_PAUSE, 1, 0, &ret) == HC_OK);
    TEST_ASSERT(vm_is_paused(&vm[1]));
}

void cpu_arch_irq_enable(void) {}
void cpu_arch_irq_disable(void) {}
void cpu_arch_timer_tick_arm(void) {}
void cpu_arch_kick(u32 pcpu) { kicks[pcpu]++; }
void cpu_arch_idle(void) { longjmp(finished, 1); }

void vcpu_arch_put(struct vcpu *v)
{
    TEST_ASSERT(v == resident);
    TEST_ASSERT(v->state == VCPU_RUNNABLE);
}

void vcpu_arch_load(struct vcpu *v)
{
    resident = v;
    if (pause_in_load) {
        /* Request arrives after pick's check, before LOAD and final entry. */
        pause_target();
        TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    }
}

void vcpu_arch_resume(struct vcpu *v) { vcpu_arch_load(v); }

void vcpu_arch_run(struct vcpu *v)
{
    TEST_ASSERT(v == resident);
    TEST_ASSERT(v == &vm[1].vcpu[0]);
    entries++;
    /* An entry after the controlled request must have been rejected. */
    TEST_ASSERT(!vm_is_paused(v->owner));
    if (pause_in_load || entries == 2)
        longjmp(finished, 1);
    /* An ordinary unwind, not a tick: leave the resident RUNNING, exactly
     * the path which sched_loop fast-reenters without consulting pause. */
    pause_target();
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
}

static void reset(void)
{
    for (u32 i = 0; i < NR_VMS; i++) {
        vm[i] = (struct vm){ .id = i, .config = &config };
        for (u32 j = 0; j < VCPUS_PER_VM; j++) {
            vm[i].vcpu[j].owner = &vm[i];
            vm[i].vcpu[j].vcpu_idx = j;
        }
    }
    for (u32 p = 0; p < NR_CPUS; p++) {
        percpu[p] = (struct percpu){ .cpu_id = p };
        kicks[p] = 0;
    }
    resident = NULL;
    entries = 0;
    pause_in_load = false;
    x86_this_percpu = &percpu[0];
    sched_init();
}

static void run_until_idle(u32 pcpu)
{
    x86_this_percpu = &percpu[pcpu];
    if (!setjmp(finished))
        sched_loop(pcpu);
    /* longjmp leaves the hardware idle hook; the harness closes its window. */
    percpu[pcpu].idle_window = 0;
}

static void running_entry_case(bool in_load)
{
    reset();
    pause_in_load = in_load;
    vm[1].vcpu[0].state = VCPU_RUNNABLE;
    sched_enqueue(&vm[1].vcpu[0]);
    run_until_idle(0);
    TEST_ASSERT(entries == (in_load ? 0U : 1U));
    printf("%s: guest entries=%u\n", in_load ? "load-entry" : "running-fast-path", entries);
}

static void idle_pending_case(void)
{
    reset();
    /* OFF secondary, no queue and no timer: pause still needs its safe point.
     * This is the production final sleep predicate, sampled after a request
     * and before the next scheduler safe point, not a private idle model. */
    pause_target();
    TEST_ASSERT(sched_queue_empty(1));
    TEST_ASSERT(!sched_idle_may_sleep(1));
    TEST_ASSERT(kicks[0] != 0 && kicks[1] != 0);
    printf("idle-pending: affinity kicks=%u,%u\n", kicks[0], kicks[1]);
}

static void query_closure_case(void)
{
    reset();
    pause_target();
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    run_until_idle(0);
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    run_until_idle(1);
    u64 ret = query();
    TEST_ASSERT(ret & (1ULL << 32));
    TEST_ASSERT((ret & 0xffffffffULL) == 1); /* id=1, non-service, all OFF */
    TEST_ASSERT(ret & EXPECT_EXEC_QUIESCED);
    printf("query-closure: after both scheduler loops query=0x%llx\n",
           (unsigned long long)ret);
}

int main(void)
{
    running_entry_case(false);
    running_entry_case(true);
    idle_pending_case();
    query_closure_case();
    printf("pause_confirmation %s (%d assertions, %d failed)\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed != 0;
}
