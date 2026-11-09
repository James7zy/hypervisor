/* SPDX-License-Identifier: TBD */
#ifndef HV_SCHED_H
#define HV_SCHED_H

#include <types.h>
#include <sched_q.h> /* SCHED_Q_MAX for the capacity bind */
#include <vm.h>       /* NR_VMS/VCPUS_PER_VM */

/*
 * Scheduler glue (M11 slice B1). Per-pCPU round-robin over statically
 * affined vCPUs: sched_pick dequeues the head, sched_yield_cur re-enqueues
 * the current one — RR by composition. Mutual exclusion: one spinlock per
 * queue (CPU_ON enqueues cross-pCPU while the owner dequeues); both call
 * sites run with interrupts masked, satisfying spinlock.h's precondition.
 * The queue primitives (<sched_q.h>) stay lockless and host-testable.
 */
_Static_assert(NR_VMS * VCPUS_PER_VM <= SCHED_Q_MAX,
               "per-pCPU queue capacity must cover the config's vCPU count");

struct vcpu;

/* Preemption quantum (M11 slice D): the EL2 tick preempts a running vCPU
 * after this much time on the pCPU, so two RUNNING/RUNNABLE vCPUs on one
 * pCPU make progress without either yielding. 10 ms @ 100 Hz: safe on QEMU
 * TCG and real hardware alike. The arch derives it from CNTFRQ_EL0 at
 * runtime (no hard-coded frequency). */
#define SCHED_QUANTUM_MS 10

void          sched_init(void);             /* build + fill NR_CPUS queues */
struct vcpu  *sched_pick(u32 pcpu);         /* rotate out the next; NULL if idle */
void          sched_yield_cur(void);        /* re-enqueue the current vCPU */
void          sched_enqueue(struct vcpu *v);/* CPU_ON / wake (idempotent) */
void          sched_wake(struct vcpu *v);  /* BLOCKED only: WAKE + enqueue + kick */
/* Asynchronous pause requests every affinity CPU's safe point. Resume queues
 * RUNNABLE/ON_PENDING only. Nonzero means repeated state, CAS loss or exhaustion. */
int           sched_vm_pause(struct vm *v);
int           sched_vm_resume(struct vm *v);
bool          sched_queue_empty(u32 pcpu); /* locked probe (WFI "alone?" leg) */
/* Pending execution confirmation, including non-current VMs (IRQ-safe read). */
bool          sched_pause_pending(u32 pcpu);
/* Final sleep predicate; caller must have local IRQ masked. */
bool          sched_idle_may_sleep(u32 pcpu);
/* The scheduler run loop both pCPUs drop into after their bring-up (M11 B2
 * hoist; noreturn). */
void          sched_loop(u32 pcpu) __attribute__((noreturn));

/* Preemption request flag, per pCPU (M11 slice D). The timer's IRQ context
 * only raises it and unwinds — state conversion, queueing and the switch
 * must stay in sched_loop (one path through vcpu_state_step). */
void          sched_preempt_request(void);
bool          sched_preempt_take(void);   /* clear-and-test, in sched_loop */

#endif /* HV_SCHED_H */
