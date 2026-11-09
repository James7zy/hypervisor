/* SPDX-License-Identifier: TBD */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <vm_config.h>
#include "stage2_build.h"
#include "host_test.h"

int tests_run, tests_failed;
static u64 pages[S2_POOL_PAGES * 512] __attribute__((aligned(4096)));
static u64 root[512] __attribute__((aligned(4096)));

int printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int rc = vprintf(fmt, ap);
    va_end(ap);
    return rc;
}

int main(void)
{
    struct s2_pool pool;

    for (u32 i = 0; i < NR_VMS; i++) {
        memset(root, 0, sizeof(root));
        TEST_ASSERT(s2_pool_init(&pool, pages, S2_POOL_PAGES) == 0);
        TEST_ASSERT(stage2_build(root, &pool, &vm_configs[i]) == 0);
    }
    printf("stage2_build: %d assertions, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
