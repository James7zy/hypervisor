# HV-011/012（第一刀）— Stage-2 表分配器与 fault 闭环 实施计划

> **For agentic workers:** superpowers:subagent-driven-development / executing-plans；每 Task 后跑 gate，绿了才 commit。

**Goal:** Stage-2 从"两张硬编码静态表"变成"分配器 + 可复用映射 API"，并把"未处理 fault
停 pCPU"换成"park 该 vCPU"；两者都用 host 单测与新 QEMU 场景钉住。

**Spec:** [2026-11-08-hv011-stage2-allocator-design.md](../specs/2026-11-08-hv011-stage2-allocator-design.md)（G1–G7）。
**分支:** `feature/hv011-s2-tables`（`dev` `03232ad` 拉出）。

## Global Constraints

- **fault 路径禁止 `cpu_arch_halt()`**：未处理的数据异常只能 park 当前 vCPU。
- **等价性**：新 API 重建的初始映射必须与旧静态值逐字节一致（G6 启动自检），既有 12 场景零改动。
- host 层（池 + 描述符编码）**不得包含 arch 头**，必须能 host 编译；arch 文件只做 `dsb/tlbi/寄存器`。
- 不改 `struct vcpu`/`struct percpu` 的 asm 可见前缀；`check_offsets` 必须过。
- 环境：`PATH=/tmp/wrapbin:$PATH`、`LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib`、`CROSS_COMPILE=aarch64-linux-gnu-`；改头文件先 `make clean`。

## Review Focus

| # | 失败模式 | 钉住它的测试 |
|---|---|---|
| 1 | 重构后映射悄悄变了（RAM 少一块、GIC 洞没了） | G6 启动自检 + 既有 12 场景（含 `xvm`/`xvm2`/Linux 门禁） |
| 2 | 一个 guest fault 让 pCPU 停下，另一个 VM 一起死 | `s2fault` 场景：VM0 vCPU0 fault 后 VM1 仍打印标记；host 无法覆盖，靠场景 |
| 3 | 表页泄漏/池耗尽后静默继续 | host：分配-释放-再分配循环、耗尽返回 `-ENOMEM` |
| 4 | 改完描述符没刷 TLB（guest 读到旧映射） | host 断言"需要失效"的返回值；arch 侧 `tlbi` 调用点由代码评审 + `s2fault` 场景（新映射可见） |

## Task 1: host 可测的表池与映射 API（RED → GREEN）

- RED commit（仅测试 + Makefile 目标）：`tests/host/s2_test.c`，断言
  池分配/释放/耗尽、`s2_map` 建 L2/L3、块与页混用、`s2_unmap` 回收、`s2_protect` 只改属性、
  属性/XN 编码、返回值"需要失效"。
- GREEN：新增 `hypervisor/common/mmu/s2_table.c` + `include/s2_table.h`（**generic 目录**，
  arch-neutral，纯内存操作），提供：
  ```c
  struct s2_pool { u64 *pages; u32 npages; u64 *bitmap; };  /* 4 KB 表页 */
  int  s2_pool_init(struct s2_pool *, void *mem, u32 npages);
  u64 *s2_pool_alloc(struct s2_pool *);   /* NULL = 耗尽 */
  void s2_pool_free(struct s2_pool *, u64 *);
  int  s2_map(u64 *root, struct s2_pool *, u64 ipa, u64 pa, u64 len, u32 attr, bool *flush);
  int  s2_unmap(...); int s2_protect(...);
  ```
  属性常量（`S2_ATTR_*`/`S2_PROT_*`/`S2_XN`）放该头；arch 只把常量翻成硬件位。
- 验证：`make host-test` 五套全绿。

## Task 2: arch 侧接上（等价 + TLB + 自检）

- `arch/arm64/mmu/stage2.c` 重写为：静态池（`S2_POOL_PAGES`）+ `s2_map` 建初始映射
  （设备 2 MB 块、RAM 1 GB 块、GIC/UART 不打洞而是不映射）→ 启动自检比对旧值 →
  `stage2_activate` 不变。
- 新增 `stage2_flush_vm()`（`tlbi vmalls12e1is` + `dsb ish`），在任何 map/protect 后调用。
- `MAIR_EL2` 显式写两个槽位（Normal WB / Device nGnRE），不再依赖复位值。
- 验证：`make test` 12 场景仍绿（等价性证明）。

## Task 3: fault 闭环 + 场景

- `arch/arm64/vmexit/vmexit.c` 的 `default:` 不再 `cpu_arch_halt()`：对 `EC_DABT_LOW` 且
  MMIO 处理器返回非 0 的情形，走 `s2_fault_kill_current()`（`vcpu_state_step(EV_OFF)` +
  打印 + `hv_restore()`）；其它未知 EC 保留原有诊断但改为 park 当前 vCPU（不停 pCPU）。
- 新 SVM 场景 `s2fault`：VM0 vCPU0 访问未映射 IPA → 期望 `[hv] s2 fault` 行 + VM1 的标记行；
  `tests/svm/expect/s2fault.txt`；`test-qemu-s2fault` 加入 `test` 聚合（12 → 13）。
- 验证：`make test` 13/13；`make test-qemu-s2fault` ×3 稳定；`make test-linux` 绿。

## Task 4: 收尾

- 架构师写 ADR-0025、devlog、报告；Linear（H-125/H-123/H-127 评论）；合回 `dev` 并 push。

## 不在本片

- guest RAM 动态分配、`io_req` 到 DM、TLB range 失效、巨页拆分策略、按需调页。
