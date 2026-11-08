# HV-005 — Hypercall ABI 与陷入分发 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** guest 能用 `hvc #0` 查到 hypercall 版本与 VM 的派生态，越权调用被拒；分发/权限/编码
都是 host 可测的纯逻辑，arch 只负责摊参数。

**Architecture:** generic 新增 `common/hypercall/`（表 + 分发 + 权限），arch 的
`vmexit.c handle_hvc()` 在既有 SMCCC/PSCI 分支之后把 vendor 范围 id 转进来，只传 `u64`。
VM 派生态从 `dm/hv_shell.c` 抽到 `common/vm/vm.c` 共用。

**Tech Stack:** freestanding C11、host 单测（`make host-test`）、QEMU `svm_dual` 场景。

**Spec:** [2026-11-08-hv005-hypercall-abi-design.md](../specs/2026-11-08-hv005-hypercall-abi-design.md)。
**分支:** `feature/hv005-hypercall-abi`（`dev` `57bf8df` 拉出）。

## Global Constraints

- **调用者身份只来自 `current_vcpu()->owner`**，任何从参数取身份的写法视为 bug。
- **已发布 id 只增不改**：`HC_GUEST_DONE` 必须仍是 `0x87000001`，行为不变。
- **generic 不得包含 arch 头**（`make test` 的 `check-arch-boundary` 零容忍）；`hypercall.c`
  只依赖 `<types.h> <vm.h> <hypercall.h>`。
- **不动 `struct percpu` / `struct vcpu` 的 asm 可见前缀**；`check_offsets` 必须保持通过。
- 每 Task 后 `make host-test` + `make test` 全绿；改头文件先 `make clean`。
- 环境：`export PATH=/tmp/wrapbin:$PATH
  LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib CROSS_COMPILE=aarch64-linux-gnu-`。

## Review Focus

| # | 失败模式 | 钉住它的测试 |
|---|---|---|
| 1 | guest 传 `x1=0` 自称 Service VM 就能越权 | host 用"非 service caller + 他人 vmid"断言 `HC_EPERM`；SVM 场景 VM1 查 VM0 得 EPERM |
| 2 | 未知 id 被当成功（或回正数） | host 与 SVM 场景都断言 `HC_ENOSYS`（负值） |
| 3 | `HC_GUEST_DONE` 的 id/行为被顺手改掉，SVM 用例集体红 | 既有 11 场景零改动仍绿；host 断言该 id 值 |
| 4 | 派生状态两处实现漂移（shell 说 halted、hypercall 说 online） | 两处共用 `vm_any_vcpu_on()`/`vm_online_vcpus()`；host 直接测这两个函数 |

## Task 1: 派生态抽取 + hypercall 核心（host RED→GREEN）

**RED commit（仅测试）:**
- 新建 `tests/host/hypercall_test.c`：定义 `struct vm vm[3]` + 需要的假钩子，断言
  （`HC_VERSION`、自查询、Service 查他人、非 Service 查他人 → `HC_EPERM`、未知 id →
  `HC_ENOSYS`、越界 vmid → `HC_ENODEV`、返回字三段编码、`HC_GUEST_DONE == 0x87000001`）。
- Makefile 增加 `$(BUILD_DIR)/host/hypercall_test` 目标与 `host-test` 依赖。
- 该 commit 必须**编译/链接失败**（`hypercall_dispatch` 未声明）。

**GREEN:**
- `hypervisor/include/hypercall.h`（新）：E1/E4 常量 + `hypercall_dispatch()` 原型 + `struct vm` 前向声明。
- `hypervisor/common/hypercall/hypercall.c`（新）：表 `{ id, role_mask, fn }`、分发、`HC_VERSION`、
  `HC_VM_QUERY`。表项按 id 升序；未知 id → `HC_ENOSYS`；越界/不存在 vmid → `HC_ENODEV`。
- `hypervisor/include/vm.h` + `hypervisor/common/vm/vm.c`：`vm_is_service()`、
  `vm_online_vcpus()`、`vm_any_vcpu_on()`。
- `hypervisor/dm/hv_shell.c`：`vm_list` 改用共用函数（行为不变，输出字符串不变）。
- `hypervisor/Makefile`：`hv-objs += common/hypercall/hypercall.o`。
- 验证：`make host-test` 三套 + 新增一套全绿。

## Task 2: arch 接线（ARM64）+ x86 链接检查

- `hypervisor/arch/arm64/include/arch/hypercall.h`：删掉重复的 `HVC_VENDOR_BASE`/`HC_GUEST_DONE`
  定义，改为 `#include <hypercall.h>`（generic 是唯一真相源；SMCCC 常量留在原处）。
- `hypervisor/arch/arm64/vmexit/vmexit.c` 的 `handle_hvc()`：
  ```c
  if ((func_id & 0xFF000000U) == HVC_VENDOR_BASE) {
      if (func_id == HC_GUEST_DONE) { ... 原样（noreturn）... }
      u64 ret = 0;
      s64 rc = hypercall_dispatch(current_vcpu()->owner, func_id, regs->x[1], regs->x[2], &ret);
      regs->x[0] = (u64)rc;
      regs->x[1] = ret;
      return;
  }
  ```
  顺序不变：SMCCC base 探测 → PSCI `0x84/0xC4` → vendor 分发 → 未知。
- 验证：`make test` 既有 11 场景仍绿；`make test-x86-stub` 通过（generic 模块无 arch 依赖）。

## Task 3: SVM 门禁场景 `hcall`（聚合 11 → 12）

- `tests/svm/cases/hcall.c`（新，`svm_dual` profile，**同一二进制给两个 VM**）：
  - 先 `HC_VM_QUERY(self)` 拿 vmid；
  - `HC_VERSION` 必须成功且 `x1` 高 16 位 = 0；
  - 自查询成功；查"另一个"VM：Service 期望成功、非 Service 期望 `-2`；
  - 未知 id（`0x8700FF00`）期望 `-3`；
  - 打印固定标记，最后 `HC_GUEST_DONE`（沿用既有退出路径）。
- `tests/svm/expect/hcall.txt`：两个 VM 各自的标记行。
- `Makefile`：`SVM_CASES += hcall`（注意 `svm_dual` profile 的 `SVM_BIN`/`SVM_BIN2` 都指向它），
  新增 `test-qemu-hcall` 并加入 `test` 聚合。
- 验证：`make test` 12/12；`make test-qemu-hcall` 连跑 3 次稳定。

## Task 4: 收尾

- 架构师写 ADR-0022（ABI 形状/权限/兼容承诺）、devlog、当日报告。
- Linear：H-113 关闭、H-107（HV-004）置 In Progress 并写下一片范围。
- 线性合回 `dev`，push `origin/develop`。

## 不在本片

- VM 生命周期迁移与事件（HV-004，下一片）。
- 结构体/大块参数 ABI、`copy_from_gpa` 一类语义。
- HSM 调用、`io_req`、MMIO 分发。
