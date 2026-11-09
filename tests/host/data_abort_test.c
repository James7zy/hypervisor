/* SPDX-License-Identifier: TBD */
#include <mmio.h>
#include <string.h>
#include "data_abort.h"
#include "host_test.h"

int tests_run;
int tests_failed;
static int calls;
static int handler_result;
static struct mmio_access captured;

int printk(const char *fmt, ...)
{
    (void)fmt;
    return 0;
}

static int spy(struct mmio_access *acc, void *ctx)
{
    (void)ctx;
    calls++;
    captured = *acc;
    acc->data = 0xabcdef;
    return handler_result;
}

static struct abort_snapshot snapshot(u32 fsc)
{
    return (struct abort_snapshot) {
        .esr = (0x24ULL << 26) | (1U << 24) | (2U << 22) |
               (3U << 16) | fsc,
        .far = 0x1080, .hpfar = 0x10, .elr = 0x8000,
    };
}

static void rejected(struct abort_snapshot fault)
{
    struct arch_regs regs = { .x = { 1, 2, 3, 4 }, .elr_el2 = fault.elr };
    struct arch_regs before = regs;
    calls = 0;
    TEST_ASSERT(mmio_handle_data_abort(&regs, &fault) != 0);
    TEST_ASSERT(calls == 0);
    TEST_ASSERT(memcmp(&regs, &before, sizeof(regs)) == 0);
}

int main(void)
{
    TEST_ASSERT(mmio_bus_register(0x1000, 0x1000, spy, NULL) == 0);
    /* 陈旧 HPFAR 指向真实注册的 spy；旧 ISV-only 路径会 dispatch 并改寄存器。 */
    rejected(snapshot(0x0f));

    struct abort_snapshot fault = snapshot(0x07);
    struct arch_regs regs = { .elr_el2 = fault.elr };
    calls = 0;
    TEST_ASSERT(mmio_handle_data_abort(&regs, &fault) == 0);
    TEST_ASSERT(calls == 1 && captured.addr == 0x1080);
    TEST_ASSERT(captured.size == 4 && !captured.is_write);
    TEST_ASSERT(regs.x[3] == 0xabcdef && regs.elr_el2 == fault.elr + 4);
    printf("data_abort_test: %d assertions, %d failures\n", tests_run, tests_failed);
    return tests_failed != 0;
}
