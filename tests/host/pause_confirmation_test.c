/* SPDX-License-Identifier: TBD */
/* T1 behavioral regression: real sched/vm/hypercall, only hardware replaced. */
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
static void (*run_action)(struct vcpu *);
static void (*load_action)(struct vcpu *);
static void (*point_action)(void);
static enum pause_test_event armed_point;
static bool irq_masked, idle_returns;
static bool irq_pending[NR_CPUS];
static unsigned sleeps, irq_windows, put_target;

static void arm_point(enum pause_test_event event, void (*action)(void))
{
    armed_point = event;
    point_action = action;
}

void pause_test_point(enum pause_test_event event, const struct vm *v)
{
    if (point_action && event == armed_point && (!v || v == &vm[1])) {
        void (*action)(void) = point_action;
        point_action = NULL; /* permit a competing production operation */
        action();
    }
}
#define EXPECT_EXEC_QUIESCED HC_VM_QUERY_EXEC_QUIESCED

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

void cpu_arch_irq_enable(void)
{
    TEST_ASSERT(irq_masked && x86_this_percpu->idle_window);
    irq_masked = false;
    irq_pending[current_vcpu_id()] = false; /* stand-in for IRQ consumption, not ack */
    irq_windows++;
}
void cpu_arch_irq_disable(void)
{
    TEST_ASSERT(!irq_masked && x86_this_percpu->idle_window);
    irq_masked = true;
}
void cpu_arch_timer_tick_arm(void) {}
void cpu_arch_kick(u32 pcpu)
{
    kicks[pcpu]++;
    irq_pending[pcpu] = true;
}
void cpu_arch_idle(void)
{
    TEST_ASSERT(irq_masked && x86_this_percpu->idle_window);
    sleeps++;
    if (!idle_returns)
        longjmp(finished, 1);
    /* A pending physical interrupt is the only modeled reason to return. */
    TEST_ASSERT(irq_pending[current_vcpu_id()]);
}

void vcpu_arch_put(struct vcpu *v)
{
    TEST_ASSERT(v == resident);
    TEST_ASSERT(v->state == VCPU_RUNNABLE);
    if (v == &vm[1].vcpu[0])
        put_target++;
}

void vcpu_arch_load(struct vcpu *v)
{
    resident = v;
    if (load_action) {
        load_action(v);
        return;
    }
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
    entries++;
    if (run_action) {
        run_action(v);
        return;
    }
    TEST_ASSERT(v == &vm[1].vcpu[0]);
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
        irq_pending[p] = false;
    }
    resident = NULL;
    entries = 0;
    pause_in_load = false;
    run_action = load_action = NULL;
    point_action = NULL;
    irq_masked = true;
    idle_returns = false;
    sleeps = irq_windows = put_target = 0;
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
    TEST_ASSERT(ret & HC_VM_QUERY_PAUSED);
    TEST_ASSERT((ret & 0xffffffffULL) == 1); /* id=1, non-service, all OFF */
    TEST_ASSERT(ret & EXPECT_EXEC_QUIESCED);
    printf("query-closure: after both scheduler loops query=0x%llx\n",
           (unsigned long long)ret);
}

static void resume_target(void)
{
    u64 ret = 0;
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_RESUME, 1, 0, &ret) == HC_OK);
    TEST_ASSERT(!vm_is_paused(&vm[1]));
}

static void confirm_both(void)
{
    run_until_idle(0);
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    run_until_idle(1);
    TEST_ASSERT(query() & EXPECT_EXEC_QUIESCED);
}

static void stop_run(struct vcpu *v)
{
    TEST_ASSERT(!vm_is_paused(v->owner));
    longjmp(finished, 1);
}

static void pause_at_fast_gate(struct vcpu *v)
{
    TEST_ASSERT(v == &vm[1].vcpu[0]);
    TEST_ASSERT(!vm_is_paused(v->owner));
    if (entries != 1)
        longjmp(finished, 1);
    arm_point(PAUSE_SAFE_POINT_DONE, pause_target);
}

static void fast_gate_case(void)
{
    reset();
    vm[1].vcpu[0].state = VCPU_RUNNABLE;
    sched_enqueue(&vm[1].vcpu[0]);
    run_action = pause_at_fast_gate;
    run_until_idle(0);
    TEST_ASSERT(entries == 1 && !point_action);
    TEST_ASSERT(vm[1].pause_ack[0] == vm[1].pause_state);
}

static void states_and_affinity_case(void)
{
    for (enum vcpu_state st = VCPU_OFF; st <= VCPU_BLOCKED; st++) {
        if (st == VCPU_RUNNING)
            continue; /* RUNNING needs an actual resident; covered separately. */
        reset();
        struct vcpu *v = &vm[1].vcpu[0];
        v->state = st;
        if (st == VCPU_RUNNABLE || st == VCPU_ON_PENDING)
            sched_enqueue(v);
        pause_target();
        TEST_ASSERT(kicks[0] == 1 && kicks[1] == 1);
        TEST_ASSERT(sched_pause_pending(0) && sched_pause_pending(1));
        confirm_both();
        TEST_ASSERT(entries == 0 && v->state == st);
        resume_target();
        TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
        TEST_ASSERT(v->state == st);
        TEST_ASSERT(sched_pick(0) ==
                    ((st == VCPU_RUNNABLE || st == VCPU_ON_PENDING) ? v : NULL));
    }
    reset();
    static const struct vm_config shared = { .vcpu_pcpu = {0, 0} };
    vm[1].config = &shared;
    pause_target();
    TEST_ASSERT(kicks[0] == 1 && kicks[1] == 0);
    TEST_ASSERT(!sched_pause_pending(1));
    run_until_idle(0);
    TEST_ASSERT(query() & EXPECT_EXEC_QUIESCED);
    TEST_ASSERT(vm[1].pause_ack[1] == 0);
}

static void resume_saw_running(void)
{
    TEST_ASSERT(vm[1].vcpu[0].state == VCPU_RUNNING);
    TEST_ASSERT(sched_queue_empty(0));
    resume_target();
    TEST_ASSERT(sched_queue_empty(0)); /* resume deliberately did not enqueue */
}

static void pause_then_resume_running(struct vcpu *v)
{
    TEST_ASSERT(v == &vm[1].vcpu[0]);
    if (entries == 1) {
        pause_target();
        arm_point(PAUSE_BEFORE_EXCLUDE, resume_saw_running);
    } else {
        stop_run(v);
    }
}

static void resume_running_exclude_case(void)
{
    reset();
    vm[1].vcpu[0].state = VCPU_RUNNABLE;
    sched_enqueue(&vm[1].vcpu[0]);
    run_action = pause_then_resume_running;
    run_until_idle(0);
    TEST_ASSERT(entries == 2 && !point_action);

    /* Same race at the post-LOAD gate: hardware is already the target's. */
    reset();
    vm[1].vcpu[0].state = VCPU_RUNNABLE;
    sched_enqueue(&vm[1].vcpu[0]);
    pause_in_load = true;
    arm_point(PAUSE_BEFORE_EXCLUDE, resume_saw_running);
    run_action = stop_run;
    run_until_idle(0);
    TEST_ASSERT(entries == 1 && !point_action);
    TEST_ASSERT(resident == &vm[1].vcpu[0] && resident->launched);
}

static void load_pause_target(struct vcpu *v)
{
    if (v == &vm[1].vcpu[0])
        pause_target();
}

static void yield_service_then_stop(struct vcpu *v)
{
    TEST_ASSERT(v == &vm[0].vcpu[0]);
    if (entries == 1) {
        TEST_ASSERT(vcpu_state_step(&v->state, VCPU_EV_YIELD) == 0);
        sched_yield_cur();
    } else {
        TEST_ASSERT(put_target == 1); /* must put B, not pretend A stayed resident */
        TEST_ASSERT(vm[1].vcpu[0].launched);
        stop_run(v);
    }
}

static void load_resident_case(void)
{
    for (unsigned launched = 0; launched <= 1; launched++) {
        reset();
        for (u32 i = 0; i < NR_VMS; i++) {
            vm[i].vcpu[0].state = VCPU_RUNNABLE;
            vm[i].vcpu[0].launched = launched;
            sched_enqueue(&vm[i].vcpu[0]);
        }
        run_action = yield_service_then_stop;
        load_action = load_pause_target;
        run_until_idle(0);
        TEST_ASSERT(entries == 2 && put_target == 1);
    }
}

static void resume_after_safe_point(void) { resume_target(); }

static void fresh_pick_case(void)
{
    reset();
    vm[1].vcpu[0].state = VCPU_RUNNABLE;
    sched_enqueue(&vm[1].vcpu[0]);
    pause_target();
    arm_point(PAUSE_AFTER_ACK, resume_after_safe_point);
    run_action = stop_run;
    run_until_idle(0);
    TEST_ASSERT(entries == 1 && !point_action);
}

static void publisher_after_resume_case(void)
{
    for (unsigned wake = 0; wake <= 1; wake++) {
        reset();
        struct vcpu *v = &vm[1].vcpu[0];
        v->state = wake ? VCPU_BLOCKED : VCPU_OFF;
        pause_target();
        resume_target(); /* scan completes before the publisher's transition */
        TEST_ASSERT(sched_queue_empty(0));
        if (wake) {
            sched_wake(v);
        } else {
            /* Actual CPU_ON state/enqueue boundary from vpsci; the ARM
             * register authoring before this boundary is not host-executable. */
            TEST_ASSERT(vcpu_state_step(&v->state, VCPU_EV_CPU_ON) == 0);
            sched_enqueue(v);
            cpu_arch_kick(0);
        }
        TEST_ASSERT(!sched_queue_empty(0));
        pause_target();
        confirm_both();
        TEST_ASSERT(entries == 0);
        resume_target();
        run_action = stop_run;
        run_until_idle(0);
        TEST_ASSERT(entries == 1 && v->state == VCPU_RUNNING);
    }
}

static u64 old_epoch;
static void inspect_late_ack(void)
{
    TEST_ASSERT(vm[1].pause_ack[1] == old_epoch);
    TEST_ASSERT(vm[1].pause_state != old_epoch);
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    longjmp(finished, 1);
}

static void replace_epoch_before_ack(void)
{
    resume_target();
    pause_target();
    arm_point(PAUSE_AFTER_ACK, inspect_late_ack);
}

static void epochs_case(void)
{
    reset();
    pause_target();
    confirm_both();
    old_epoch = vm[1].pause_state;
    resume_target();
    pause_target();
    TEST_ASSERT(vm[1].pause_state != old_epoch);
    TEST_ASSERT(vm[1].pause_ack[0] == old_epoch && vm[1].pause_ack[1] == old_epoch);
    TEST_ASSERT(!(query() & EXPECT_EXEC_QUIESCED));
    run_until_idle(0);
    old_epoch = vm[1].pause_state;
    arm_point(PAUSE_BEFORE_ACK, replace_epoch_before_ack);
    run_until_idle(1); /* late old ack is the real scheduler store */
    TEST_ASSERT(!point_action);
    confirm_both();
}

static void query_resume(void) { resume_target(); }
static void query_replace(void)
{
    resume_target();
    pause_target();
    confirm_both(); /* even all NEW acks cannot make the old observation stable */
}

static void query_change_case(void)
{
    reset();
    pause_target();
    confirm_both();
    arm_point(QUERY_AFTER_ACKS, query_resume);
    u64 ret = query();
    TEST_ASSERT(!(ret & (HC_VM_QUERY_PAUSED | EXPECT_EXEC_QUIESCED)));
    pause_target();
    confirm_both();
    arm_point(QUERY_AFTER_ACKS, query_replace);
    ret = query();
    TEST_ASSERT((ret & HC_VM_QUERY_PAUSED) && !(ret & EXPECT_EXEC_QUIESCED));
    TEST_ASSERT(query() & EXPECT_EXEC_QUIESCED);
}

static void competing_pause_resume(void) { pause_target(); resume_target(); }
static void competing_resume_pause(void) { resume_target(); pause_target(); }

static void cas_and_exhaustion_case(void)
{
    reset();
    u64 ret = 0;
    arm_point(PAUSE_BEFORE_CAS, competing_pause_resume);
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_PAUSE, 1, 0, &ret) == HC_EBUSY);
    TEST_ASSERT(vm[1].pause_state == 2 && !point_action);
    pause_target();
    arm_point(RESUME_BEFORE_CAS, competing_resume_pause);
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_RESUME, 1, 0, &ret) == HC_EBUSY);
    TEST_ASSERT(vm[1].pause_state == 7 && !point_action);
    reset();
    /* Boundary fixture only; requests, acks and QUERY still execute production. */
    vm[1].pause_state = ~3ULL;
    pause_target();
    TEST_ASSERT(vm[1].pause_state == ~0ULL);
    confirm_both();
    resume_target();
    TEST_ASSERT(vm[1].pause_state == ~1ULL);
    TEST_ASSERT(hypercall_dispatch(&vm[0], HC_VM_PAUSE, 1, 0, &ret) == HC_EBUSY);
    TEST_ASSERT(vm[1].pause_state == ~1ULL);
}

static void stop_after_idle_ack(void)
{
    TEST_ASSERT(irq_masked && !x86_this_percpu->idle_window);
    TEST_ASSERT(irq_windows == 1 && !irq_pending[0]);
    TEST_ASSERT(vm[1].pause_ack[0] == vm[1].pause_state);
    longjmp(finished, 1);
}

static void idle_request(void)
{
    TEST_ASSERT(irq_masked && x86_this_percpu->idle_window);
    pause_target();
    TEST_ASSERT(irq_pending[0]);
    TEST_ASSERT(vm[1].pause_ack[0] != vm[1].pause_state);
    arm_point(PAUSE_AFTER_ACK, stop_after_idle_ack);
}

static void idle_windows_case(void)
{
    for (unsigned after_check = 0; after_check <= 1; after_check++) {
        reset();
        idle_returns = true;
        arm_point(after_check ? PAUSE_IDLE_SLEEP : PAUSE_IDLE_CHECK, idle_request);
        run_until_idle(0);
        TEST_ASSERT(!point_action && irq_windows == 1);
        TEST_ASSERT(sleeps == after_check);
        TEST_ASSERT(sched_pause_pending(1));
    }
}

int main(void)
{
    running_entry_case(false);
    running_entry_case(true);
    idle_pending_case();
    query_closure_case();
    fast_gate_case();
    states_and_affinity_case();
    resume_running_exclude_case();
    load_resident_case();
    fresh_pick_case();
    publisher_after_resume_case();
    epochs_case();
    query_change_case();
    cas_and_exhaustion_case();
    idle_windows_case();
    printf("pause_confirmation %s (%d assertions, %d failed)\n",
           tests_failed ? "FAIL" : "PASS", tests_run, tests_failed);
    return tests_failed != 0;
}
