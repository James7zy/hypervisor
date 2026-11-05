# M11 片 C 回归修复报告（进行中）

> 未完成最终验收，不是测试专家复验或架构师关卡②；尚未合回集成分支。
> [实现说明](../devlog/2026-11-05-m11-c-regression-fix.md) /
> [证据校正与定位](../debug/m11-c-survivor-freeze.md)。

## 范围与分支

本轮从本地 `dev` 建 `feature/m11-c-survivor-fix`；不操作 `master`。
目标仍是修复跨 VM SYSTEM_OFF 两项回归、定位启动挂死、补检测与验证，到可交复验为止。
没有实施片 D 的周期 tick / 时间片抢占。

## 当前提交

| 提交 | 内容 |
|---|---|
| `4aa637e` | 修正 xvm2 无等待采样，新增 active SGI2 停机受控红与 Linux 驱动 |
| `1b70fed` | 保存/恢复 AP0/AP1，撤回旧错误根因结论 |
| `e998b54` / `20012b7` | LOAD 中被 sweep 的 host RED / resident 记账修复 |
| `d693e36` / `0733716` | OFF 并发压力 RED / CAS 与原子 stop-kick 修复 |
| `ddb614f` | 布局 pins、头文件依赖，xvm2 纳入聚合 |
| `7be77bf` | queued secondary 在 WFE 等待下无法运行的检测（诊断 RED） |

## 已执行证据

```text
xvm2 active-IPI：未修 HV 3/3 RED
xvm2 AP 修复：3/3 GREEN
回退 AP 修复：RED，XVM2: CHECK FAILED，done (x1=0x1)
重新应用 AP：ALL PASS
host 旧 scheduler：两个场景 v == resident / resume_a_count 均失败
host 修复 scheduler：scheduler PASS (20 assertions)
state 旧实现：500000 轮，五种 event 丢 OFF 共 113791 次
state CAS 实现：500000 轮，五种 event 各 0 次
make host-test：PASS (195 assertions) + scheduler PASS (20 assertions) + state race PASS
全新目录 check-offsets-target：PASS
Linux driver --self-test：parser self-test PASS
```

resident 修复后、最终其他收尾改动前，Linux gate 连续 6 次通过，分别观察 VM0 两列 timer
增量及 console 应答。第六次 `(402,329) → (830,770)`。这不是最终干净树三连验收。

受控对照另在隔离的 AP-only 旧代码第 6 次捕获：

```text
[diag] pCPU1 failed LOAD VM1/v1; stale prev VM0/v1
Hardware name: hv-guest-vm1 (DT)
pc : 0x1
Kernel panic - not syncing: Oops: Fatal exception in interrupt
```

临时插桩仅在隔离工作树，工作树已删除；证据摘要及 patch 留在 `build/m11c-evidence/`。
这直接连接了 LOAD 失败分支、残留 VM1 硬件上下文与误运行 VM0 frame 后的 VM1 panic。

## 启动问题的进展和边界

旧 HV 的 focused-command boot probe：前 13 次通过，第 14 次真实挂死。两 pCPU 当前分别
运行 VM0/v0 和 VM1/v1，同 VM 的对端各在另一 pCPU 队列中。pCPU1 位于同步 IPI 的
`csd_lock_wait`，pCPU0 保存的 guest LR 指向 `multi_cpu_stop`；队列各 count=1。
因此除了 banner 交错/驱动解析错误，还确有协作调度饥饿。

只读架构师批准了 trapped-WFE 有队列时 yield 的语义，但进一步查出 QEMU 9.2.4 的
WFE 不生成 configurable trap；**不能把改 HV 的 WFE 分支当成本机修复**。正在裁定
“现有 kick 通知是否应该触发排队工作重选”的最小方案与片 C/D 边界，尚未实施。

## 专家与验证边界

本轮只读架构诊断确认 AP/prev/原子状态方案，并指出 AP 扩展遗漏的布局 pins；已修正，
重新交叉编译通过。该诊断不替代正式关卡②，也没有派测试专家执行正式复验。

未完成：启动处理方案、最终全量 `make test`、最终干净树 Linux 三连、ADR/plan/Linear
收尾。QEMU 仅验证 5 位 PreBits；6/7 位 AP 分支和 RK3588 真机没有实跑。
