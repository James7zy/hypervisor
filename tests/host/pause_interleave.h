/* SPDX-License-Identifier: TBD */
#ifndef HOST_PAUSE_INTERLEAVE_H
#define HOST_PAUSE_INTERLEAVE_H

struct vm;
enum pause_test_event {
    PAUSE_BEFORE_CAS,
    RESUME_BEFORE_CAS,
    QUERY_AFTER_ACKS,
    PAUSE_BEFORE_EXCLUDE,
    PAUSE_BEFORE_ACK,
    PAUSE_AFTER_ACK,
    PAUSE_SAFE_POINT_DONE,
    PAUSE_AFTER_PICK,
    PAUSE_IDLE_CHECK,
    PAUSE_IDLE_SLEEP,
};
void pause_test_point(enum pause_test_event event, const struct vm *v);
#define VM_PAUSE_TEST_POINT(event, v) pause_test_point(event, v)
#endif
