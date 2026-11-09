/* SPDX-License-Identifier: TBD */
#ifndef SVM_S2PERM_FIXTURE_H
#define SVM_S2PERM_FIXTURE_H

#include <s2_table.h>
struct vm;
int svm_s2perm_fixture(u64 *root, struct s2_pool *pool, const struct vm *vm);

#endif
