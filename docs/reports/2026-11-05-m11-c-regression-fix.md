# M11 片 C 回归修复报告

> 开发侧验收完成，可交测试专家复验（启动饥饿限制明确保留）；不是正式复验或关卡②。
> 未合回集成分支；用户明确要求暂缓 Linear H-27 同步，不阻塞本轮交付。
> [实现说明](../devlog/2026-11-05-m11-c-regression-fix.md) /
> [证据校正与定位](../debug/m11-c-survivor-freeze.md)。

## 范围与分支

本轮从本地 `dev` 建 `feature/m11-c-survivor-fix`；不操作 `master`。
目标仍是修复跨 VM SYSTEM_OFF 两项回归、定位启动挂死、补检测与验证，到可交复验为止。
没有实施片 D 的周期 tick / 时间片抢占。

## 当前提交

下表及既有证据中的短哈希为测试时的原始标识。按用户要求，11 条本地提交的 author /
committer 已统一为 `Jmaes7zy <1036063809@qq.com>`；逐提交验证 tree、消息、时间未变，
除身份与随之变化的 parent 哈希外无其他变化。复验使用下列映射定位当前历史：

| 原始哈希 | 当前哈希 |
|---|---|
| `4aa637e` | `11b9f1f` |
| `1b70fed` | `3e20df1` |
| `e998b54` | `005f4f5` |
| `20012b7` | `24a88c0` |
| `d693e36` | `c8e0089` |
| `0733716` | `155a2ba` |
| `ddb614f` | `077be97` |
| `7be77bf` | `c9d01f2` |
| `86e8ad3` | `a940e31` |
| `9bc4c06` | `9f7c3dc` |
| `05dd912` | `8b532b7` |

| 提交 | 内容 |
|---|---|
| `4aa637e` | 修正 xvm2 无等待采样，新增 active SGI2 停机受控红与 Linux 驱动 |
| `1b70fed` | 保存/恢复 AP0/AP1，撤回旧错误根因结论 |
| `e998b54` / `20012b7` | LOAD 中被 sweep 的 host RED / resident 记账修复 |
| `d693e36` / `0733716` | OFF 并发压力 RED / CAS 与原子 stop-kick 修复 |
| `ddb614f` | 布局 pins、头文件依赖，xvm2 纳入聚合 |
| `7be77bf` | queued secondary 在 WFE 等待下无法运行的候选检测（RED） |
| `86e8ad3` | 保留独立 wfe_wait 诊断、不进 gate；ctxsw 补 active IRQ round-trip，文档归档 |

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
WFE 不生成 configurable trap；**不能把改 HV 的 WFE 分支当成本机修复**。实际 trace 亦确认
4 秒、133 个异常中 WFx=0。无条件 kick 轮转被否决，因为可能换下刚收到 IPI 的 current，
消费唯一通知后形成新的等待环。目标关联通知协议须另行批准；本轮只定位，不进入 D，
将此场景交给既定 D 的公平调度设计。**当前启动仍可能挂死，不能以本轮绿色次数掩盖。**

独立 `wfe_wait` 诊断已编译运行：CPU_ON marker 出现、secondary marker 不出现、timeout
返回 124，符合已知限制的复现条件；这不是一个通过的硬件 gate。ctxsw 聚合保留 WFI
契约并增加 active IRQ round-trip：临时把 AP 恢复改成零时断言 RED，还原 shadow 后 GREEN。

## 最终干净树验收

代码提交 `86e8ad3`；跟踪文件干净，仅预期未跟踪目录 `.claude/`、`.pi/`。
镜像环境：QEMU 9.2.4、AArch64 GNU 13.3、Linux 6.12.103、仓库 init 脚本的 BusyBox initramfs。

| 检查 | 连续运行结果 |
|---|---|
| `make test` | 2/2 exit 0；每次 4 项静态检查、10 个 QEMU 场景 ALL PASS，约 119 秒 |
| `make host-test` | 2/2：195 assertions + scheduler 20 assertions + state race 500000 rounds |
| Linux gate 1 | boot/topology/console PASS；timer `(372,352) → (836,839)` |
| Linux gate 2 | boot/topology/console PASS；timer `(362,409) → (870,939)` |
| Linux gate 3 | boot/topology/console PASS；timer `(373,385) → (889,915)` |

三次 Linux gate 均包含 VM1 SYSTEM_OFF、VM0 双 vCPU timer workload 与关机后命令应答，
无 panic。全量日志在 `build/m11c-evidence/clean-test-{1,2}.log`、
`clean-host-test-{1,2}.log` 和 `linux-final-{1,2,3}.log`。此后仅追加验证文档。

执行注意：必须分别运行 `make test` 和 `make host-test`。现有 Makefile 对混合目标
`make test host-test` 的 need-config 判断会误跳过架构配置；本轮发现后分开运行，未扩展
范围修复这个独立构建问题，不能把那次调用失败当作硬件 gate 失败。触发条件：首个需要
混合 host/target 顶级目标的自动化入口；在此之前按上述独立命令运行。

## 专家与验证边界

本轮只读架构诊断确认 AP/prev/原子状态方案，并指出 AP 扩展遗漏的布局 pins；已修正，
重新交叉编译通过。该诊断不替代正式关卡②，也没有派测试专家执行正式复验。

仓库 ADR/devlog/plan 已同步。用户明确要求 **暂缓 Linear H-27 远端同步**，本轮未执行
远端写入，也不再以此阻塞交付。恢复条件：用户要求恢复同步且有可用接入；届时以本报告
同步两回归修复、启动根因与限制、开发 gate 结果及待正式复验状态，
**不能把 H-27/片 C 标成正式验收完成**。

QEMU 仅验证 5 位 PreBits；6/7 位 AP 分支和 RK3588 真机没有实跑。正式复验、关卡②、
合入与片 D 实现均未执行。
