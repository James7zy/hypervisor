# M11 片 D 受控红：`preempt` 门禁场景（2026-11-07）

> 目的：证明 `make test-qemu-preempt` 的判据是**承重**的——去掉 tick 抢占，场景必须红。
> 关联：[片 D 设计 §6 DoD 2](../superpowers/specs/2026-11-07-m11-slice-d-preemption-design.md)、
> [片 D 实施计划 T4](../superpowers/plans/2026-11-07-m11-slice-d-preemption.md)、
> [ADR-0019](../adr/0019-two-layer-state-division.md)（共享 pCPU 协作等待饥饿）。

## 实验方法

`tests/svm/cases/preempt.c` 的两个 vCPU 都不 WFI、不让出：vCPU0 `CPU_ON(1)` 后自旋等
vCPU1 的标记，vCPU1 自旋后打印标记再继续自旋。若不发生抢占，vCPU1 永远上不了 pCPU0。

受控变量只有一个：`hypervisor/arch/arm64/timer/vtimer.c` 里的
`cpu_arch_timer_tick_arm()`。临时在函数体首行插入 `return;`（**只在工作树里，不进 commit**）：

```diff
 void cpu_arch_timer_tick_arm(void)
 {
+    return;   /* CONTROLLED RED (M11 D T4): no tick, no preemption */
     u64 freq = SYSREG_READ(CNTFRQ_EL0);
```

命令（`make` 会按时间戳重建 hypervisor）：

```sh
make test-qemu-preempt
```

## 关键输出（红，原文摘录）

```
PASS: 'PREEMPT: v1 ran'
FAIL: 'PREEMPT: v0 observed v1' not found in output
FAIL: 'SVM HVC: done (x1=0x0)' not found in output

--- QEMU output ---
[hv] PSCI: CPU_ON vCPU1 -> ON_PENDING (pCPU0)
PREEMPT: v1 never ran
[hv] SVM HVC: done (x1=0x1)
PREEMPT: v1 ran
qemu-system-aarch64: terminating on signal 15 from pid 2432594 (timeout)
---

make: *** [Makefile:207: test-qemu-preempt] Error 1
```

`make test-qemu-preempt` 返回 1，`tests/run_svm_test.sh` 打印 FAIL 行与完整 QEMU 输出。

### 为什么第 1 行仍然 PASS

`PREEMPT: v1 ran` 出现在 vCPU0 放弃之后：vCPU0 自旋预算（20M）耗尽 → 打印
`PREEMPT: v1 never ran` → `svm_done(1)`（HC_GUEST_DONE）退出 CPU，**此时** pCPU0 才轮到
队里的 vCPU1，它补打了自己的标记并从此刻起永久自旋，直到外部 10 s 超时。

也就是说 `PREEMPT: v1 ran` 不是承重判据，承重的是后两行：

| expect 行 | 无 tick 时的表现 | 承重 |
|---|---|---|
| `PREEMPT: v1 ran` | 仍可能出现（vCPU0 放弃后才轮到） | 否 |
| `PREEMPT: v0 observed v1` | 永不出现 | **是** |
| `SVM HVC: done (x1=0x0)` | 永不出现（放弃路径是 `x1=0x1`） | **是** |

结论：**场景是承重的**——去掉 tick，门禁必红，且红的原因可读
（`PREEMPT: v1 never ran` + `done (x1=0x1)`），不是靠超时静默失败。

## 还原后（绿）

还原 `cpu_arch_timer_tick_arm()` 后连续三次 `make test-qemu-preempt`：

```
PASS: 'PREEMPT: v1 ran'
PASS: 'PREEMPT: v0 observed v1'
PASS: 'SVM HVC: done (x1=0x0)'
ALL PASS
```

三次均 `ALL PASS`；`make test` 聚合 11/11 见本轮报告。

## 已知边界

- 本实验只验证"tick 缺失 → 红"，不验证量子数值（10 ms）与公平性；`preempt` 场景不测时延。
- 受控红只在 QEMU（TCG）上做过，RK3588 真机未验证（M15）。
