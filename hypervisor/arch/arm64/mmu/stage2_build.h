/* SPDX-License-Identifier: TBD */
#ifndef ARM64_STAGE2_BUILD_H
#define ARM64_STAGE2_BUILD_H

#include <s2_table.h>

struct vm_config;

/* Initial tables only: caller supplies an empty root; failure abandons the VM. */
int stage2_build(u64 *root, struct s2_pool *pool, const struct vm_config *config);

/* Validate all initial leaves against the RAM triple and fixed device windows. */
bool stage2_build_check(const u64 *root, const struct vm_config *config);

#endif /* ARM64_STAGE2_BUILD_H */
