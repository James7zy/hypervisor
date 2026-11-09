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
    /* 所有 FSC 精确白名单；FnV 在这些类别不是通用无效位。 */
    for (u32 fsc = 0; fsc < 64; fsc++) {
        fault = snapshot(fsc);
        if (fsc < 5 || fsc > 7)
            rejected(fault);
        struct abort_info info = abort_classify(&fault);
        TEST_ASSERT(info.mmio_allowed == (fsc >= 5 && fsc <= 7));
        TEST_ASSERT(info.ipa_valid == ((fsc >= 5 && fsc <= 7) ||
                                      (fsc >= 9 && fsc <= 11)));
        TEST_ASSERT(info.far_valid == ((fsc >= 5 && fsc <= 7) ||
                                      (fsc >= 9 && fsc <= 11) ||
                                      (fsc >= 13 && fsc <= 16)));
    }
    fault = snapshot(7); fault.esr |= 1U << 7; rejected(fault);
    TEST_ASSERT(!abort_classify(&fault).ipa_valid);
    fault = snapshot(7); fault.esr |= 1U << 8; rejected(fault);
    fault = snapshot(7); fault.esr &= ~(1U << 24); rejected(fault);
    fault = snapshot(7); fault.hpfar |= 1ULL << 31; rejected(fault);
    fault = snapshot(7); fault.hpfar |= 1ULL << 39; rejected(fault);
    fault = snapshot(7); fault.hpfar |= 1ULL << 40; rejected(fault);
    for (u32 fsc = 5; fsc <= 15; fsc++) {
        fault = snapshot(fsc); fault.esr &= ~(0x3fULL << 26);
        fault.esr |= 0x20ULL << 26; rejected(fault);
        TEST_ASSERT(!abort_classify(&fault).wnr_valid);
    }
    fault = snapshot(0x10); fault.esr |= 1U << 10;
    TEST_ASSERT(!abort_classify(&fault).far_valid);
    fault = snapshot(0xf); fault.esr |= 1U << 10;
    TEST_ASSERT(abort_classify(&fault).far_valid);
    TEST_ASSERT(!abort_classify(&fault).ipa_valid);
    fault = snapshot(7); fault.esr |= 1U << 10;
    TEST_ASSERT(abort_classify(&fault).mmio_allowed);
    /* 宽度/方向/零寄存器及失败不写回。 */
    for (u32 size = 0; size < 4; size++) {
        for (u32 wr = 0; wr < 2; wr++) {
            for (u32 zero = 0; zero < 2; zero++) {
                fault = snapshot(5 + size % 3);
                fault.esr &= ~((3U << 22) | (31U << 16));
                fault.esr |= (size << 22) | ((zero ? 31U : 3U) << 16) | (wr << 6);
                regs = (struct arch_regs){ .x = { 1, 2, 3, 4 }, .elr_el2 = fault.elr };
                calls = 0;
                TEST_ASSERT(mmio_handle_data_abort(&regs, &fault) == 0);
                TEST_ASSERT(calls == 1 && captured.size == (1U << size));
                TEST_ASSERT(captured.is_write == (bool)wr);
                TEST_ASSERT(captured.data == (wr && !zero ? 4U : 0U));
                TEST_ASSERT(regs.x[3] == (!wr && !zero ? 0xabcdefU : 4U));
                TEST_ASSERT(regs.elr_el2 == fault.elr + 4);
                struct arch_regs before = regs;
                handler_result = -1;
                TEST_ASSERT(mmio_handle_data_abort(&regs, &fault) != 0);
                TEST_ASSERT(memcmp(&regs, &before, sizeof(regs)) == 0);
                handler_result = 0;
            }
        }
    }
    fault = snapshot(7); fault.hpfar = 0x200;
    rejected(fault); /* valid IPA, no registered region */
    printf("data_abort_test: %d assertions, %d failures\n", tests_run, tests_failed);
    return tests_failed != 0;
}
