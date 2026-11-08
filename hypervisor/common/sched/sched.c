/* SPDX-License-Identifier: TBD */
#include <sched.h>
#include <cpu.h>            /* cpu_arch_idle, cpu_arch_timer_tick_arm */
#include <vm_config.h>   /* vcpu_pcpu[] affinity */
#include <sched_q.h>
#include <percpu.h>
#include <spinlock.h>

/* One queue + one lock per pCPU. Static sizing: NR_CPUS queues of
 * SCHED_Q_MAX entries each — trivial next to struct vm. */
static struct sched_q  queues[NR_CPUS];
static struct spinlock q_lock[NR_CPUS];

/* Per-pCPU preemption request (slice D). Written from the timer's IRQ
 * context on this pCPU, consumed at the top of sched_loop. */
static u32 preempt_req[NR_CPUS];

void sched_preempt_request(void)
{
    __atomic_store_n(&preempt_req[current_vcpu_id()], 1u, __ATOMIC_RELEASE);
}

bool sched_preempt_take(void)
{
    return __atomic_exchange_n(&preempt_req[current_vcpu_id()], 0u,
                               __ATOMIC_ACQ_REL) != 0u;
}

void sched_init(void)
{
    for (u32 p = 0; p < (u32)NR_CPUS; p++) {
        sched_q_init(&queues[p]);
        q_lock[p] = (struct spinlock)SPINLOCK_INIT;
    }
    /* Fill from the config: every RUNNABLE boot vCPU starts queued on its
     * affinity pCPU (vm_init authors vcpu[0] RUNNABLE; vcpu[1+] wait for
     * guest CPU_OFF... CPU_ON and enqueue from vpsci). */
    for (u32 vmi = 0; vmi < (u32)NR_VMS; vmi++) {
        for (u32 i = 0; i < (u32)VCPUS_PER_VM; i++) {
            struct vcpu *v = &vm[vmi].vcpu[i];
            if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VCPU_RUNNABLE)
                (void)sched_q_enqueue(&queues[vm[vmi].config->vcpu_pcpu[i]], v);
        }
    }
}

struct vcpu *sched_pick(u32 pcpu)
{
    spin_lock(&q_lock[pcpu]);
    struct vcpu *v = sched_q_dequeue(&queues[pcpu]);
    spin_unlock(&q_lock[pcpu]);
    return v;
}

void sched_yield_cur(void)
{
    /* Queue derived from the CURRENT pCPU while sched_enqueue derives it
     * from the vCPU's affinity — equal under the B1 invariant that
     * cur_vcpu is always this pCPU's affined vCPU (static affinity, no
     * migration). B2 re-audits if that ever loosens. */
    struct vcpu *v = current_vcpu();
    u32 pcpu = current_vcpu_id();

    spin_lock(&q_lock[pcpu]);
    (void)sched_q_enqueue(&queues[pcpu], v);   /* idempotent */
    spin_unlock(&q_lock[pcpu]);
}

void sched_enqueue(struct vcpu *v)
{
    u32 pcpu = v->owner->config->vcpu_pcpu[v->vcpu_idx];

    spin_lock(&q_lock[pcpu]);
    (void)sched_q_enqueue(&queues[pcpu], v);   /* idempotent */
    spin_unlock(&q_lock[pcpu]);
}

int sched_vm_pause(struct vm *v)
{
    int r = vm_pause(v);

    if (r != 0)
        return r;
    /* Only a resident vCPU needs telling: it is the one whose pCPU would stay
     * in the guest until its next quantum. Queued ones are already about to be
     * looked at, and pick drops them (HV-004 F2). */
    for (u32 i = 0; i < (u32)VCPUS_PER_VM; i++)
        if (__atomic_load_n(&v->vcpu[i].state, __ATOMIC_ACQUIRE) == VCPU_RUNNING)
            cpu_arch_kick(v->config->vcpu_pcpu[i]);
    return 0;
}

int sched_vm_resume(struct vm *v)
{
    int r = vm_resume(v);

    if (r != 0)
        return r;
    for (u32 i = 0; i < (u32)VCPUS_PER_VM; i++) {
        u32 pcpu = v->config->vcpu_pcpu[i];

        /* BLOCKED is not ours to wake (F3): it waits for its own event, and
         * the pause must not forge one. */
        if (__atomic_load_n(&v->vcpu[i].state, __ATOMIC_ACQUIRE) != VCPU_RUNNABLE)
            continue;
        sched_enqueue(&v->vcpu[i]);
        cpu_arch_kick(pcpu);   /* its pCPU may be idling on an empty queue */
    }
    return 0;
}

/* The one scheduler loop (M11 B2, gate-2 bill 1 hoist): both pCPUs run this.
 * Shape per the B1 rulings: RUNNING fast path (no-yield unwinds re-enter
 * prev directly), pick with idle-continue and swept-skip, put(prev) only
 * on real switch-away, cur_vcpu re-bound every pick, launched fork
 * (fresh load vs resume) in BOTH the switch-in and self-repick branches.
 * Callers seed cur_vcpu and run their own bring-up before dropping in. */
void sched_loop(u32 pcpu) __attribute__((noreturn));
void sched_loop(u32 pcpu)
{
    struct vcpu *prev = NULL;

    for (;;) {
        struct vcpu *v;
        /* Quantum expired (slice D): the tick's handler only set the flag
         * and unwound via hv_restore, leaving prev RUNNING. Convert it here
         * — the same VCPU_EV_YIELD path a WFI yield takes, so every state
         * transition keeps going through vcpu_state_step — and queue it.
         * The pick below then rotates it behind whoever else is waiting. */
        if (sched_preempt_take() && prev != NULL &&
            __atomic_load_n(&prev->state, __ATOMIC_ACQUIRE) == VCPU_RUNNING) {
            if (vcpu_state_step(&prev->state, VCPU_EV_YIELD) == 0)
                sched_yield_cur();
        }
        if (prev != NULL &&
            __atomic_load_n(&prev->state, __ATOMIC_ACQUIRE) == VCPU_RUNNING) {
            /* Unwind without a state change (HC_GUEST_DONE park; a timer
             * exit erets straight back to the guest and never reaches this
             * loop): the vCPU is RUNNING and therefore OUT of the queue
             * (RR dequeues at pick; only yield re-enqueues) — re-enter it
             * directly. Picking here would see an empty queue and idle
             * forever. */
            vcpu_arch_run(prev);
            continue;
        }
        v = sched_pick(pcpu);
        if (v == NULL) {
            /* Idle window (M11 B2, tester N1 hardening): unmasks FIRST, then
             * re-checks the queue — an interrupt that was already pending
             * lands its handler rounds BETWEEN the unmask and the check, so
             * the check observes the wake and skips the wfi. (Residual
             * microsecond window: a wake completing both handler rounds in
             * the check→wfi gap — recorded in ADR-0019.) No locks held here:
             * the window is interruptible by design. */
            percpu[pcpu].idle_window = 1;
            cpu_arch_irq_enable();
            if (sched_idle_may_sleep(pcpu))
                cpu_arch_idle();
            cpu_arch_irq_disable();
            percpu[pcpu].idle_window = 0;
            continue;
        }
        if (vm_is_paused(v->owner))
            continue;                       /* paused VM: drop it, pick again */
        enum vcpu_state state = __atomic_load_n(&v->state, __ATOMIC_ACQUIRE);
        if (state != VCPU_RUNNABLE && state != VCPU_ON_PENDING)
            continue;                       /* swept: drop it, pick again */
        if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) == VCPU_ON_PENDING)
            (void)vcpu_state_step(&v->state, VCPU_EV_PICK);
        if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VCPU_RUNNABLE)
            continue;                       /* lost the race above */

        if (v != prev) {
            if (prev != NULL &&
                __atomic_load_n(&prev->state, __ATOMIC_ACQUIRE) == VCPU_RUNNABLE)
                vcpu_arch_put(prev);        /* real switch-away only */
            percpu[pcpu].cur_vcpu = v;      /* every pick re-binds */
            if (v->launched) {
                vcpu_arch_resume(v);
            } else {
                vcpu_arch_load(v);
                v->launched = true;
            }
            /* Hardware already belongs to v even if a remote sweep makes
             * LOAD fail. Never self-repick the old prev with v's context. */
            prev = v;
            if (vcpu_state_step(&v->state, VCPU_EV_LOAD) != 0)
                continue;                   /* swept mid-load: drop */
        } else {
            /* Self re-pick (prev == v): yield (RUNNABLE, launched) or this
             * pCPU's previous vCPU re-ON'd after CPU_OFF — fork on
             * launched, exactly like the switch-in branch (manual-gate
             * Day-4 crash). */
            percpu[pcpu].cur_vcpu = v;
            if (v->launched) {
                if (vcpu_state_step(&v->state, VCPU_EV_LOAD) != 0)
                    continue;
            } else {
                /* Re-ON of prev: the same pointer returns after CPU_OFF —
                 * completely different entry: the fresh-boot warm reset
                 * (SCTLR=0 + tlbi in vcpu_arch_load) is MANDATORY or the
                 * parked context's MMU-on state survives into Linux's
                 * secondary entry (manual-gate Day-4 crash). */
                vcpu_arch_load(v);
                v->launched = true;
                if (vcpu_state_step(&v->state, VCPU_EV_LOAD) != 0)
                    continue;
            }
        }
        /* Arm the next quantum right before the guest entry: this covers
         * switch-in and a self re-pick; the RUNNING fast path above inherits
         * the deadline it already has (re-arming it would restart the
         * quantum on every trap-free unwind). */
        cpu_arch_timer_tick_arm();
        vcpu_arch_run(v);
    }
}

/* Wake a BLOCKED vCPU (gate-1 R5 invariants: wake enqueues ONLY from
 * BLOCKED — RUNNING is never queued, RUNNABLE already is). Any other
 * state: idempotent no-op. */
void sched_wake(struct vcpu *v)
{
    if (__atomic_load_n(&v->state, __ATOMIC_ACQUIRE) != VCPU_BLOCKED)
        return;
    if (vcpu_state_step(&v->state, VCPU_EV_WAKE) != 0)
        return;
    sched_enqueue(v);
    cpu_arch_kick(v->owner->config->vcpu_pcpu[v->vcpu_idx]);
}

/* Locked emptiness probe — the WFI three-way's "alone?" leg (masked ctx). */
bool sched_queue_empty(u32 pcpu)
{
    spin_lock(&q_lock[pcpu]);
    bool e = sched_q_empty(&queues[pcpu]);
    spin_unlock(&q_lock[pcpu]);
    return e;
}

/*
 * ADVISORY, LOCK-FREE queue probe for the idle window (gate-2 Critical-1):
 * the window runs with IRQs unmasked, and taking q_lock there lets an
 * interrupt's enqueue path self-deadlock on the held ticket (both written
 * lock contracts forbid it; TCG never delivers IRQs mid-critical-section,
 * so only hardware would show the hang). A bare acquire read of the u8
 * count races only with the locked enqueue/dequeue; a stale-empty read
 * degrades into the already-recorded, self-healing check→wfi residual
 * (the enqueue side kicks after publishing). A stale-nonempty read merely
 * skips one idle iteration.
 */
bool sched_idle_may_sleep(u32 pcpu)
{
    return __atomic_load_n(&queues[pcpu].count, __ATOMIC_ACQUIRE) == 0;
}
