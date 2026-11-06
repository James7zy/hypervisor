# M11 片 C 回归修复 — 测试专家复验（2026-11-06）

> 分支：`feature/m11-c-survivor-fix`（从本地 `dev` 拉出）。本报告是片 C 回归修复的
> **测试专家独立复验**结果，取代上轮「开发侧验收完成、可交复验」状态。
> 上一轮：[M11 片 C 回归修复报告](2026-11-05-m11-c-regression-fix.md)；
> 实现记录：[devlog](../devlog/2026-11-06-linux-gate-rebuild.md)。

## 复验范围与结论

对 `feature/m11-c-survivor-fix` 独立跑三套 gate，不复用研发自报结果。**三 gate 全绿。**

| Gate | 结果 | exit |
|---|---|---|
| `make host-test` | 195 断言 + scheduler 20 + state race 500000 轮 0 丢失 OFF | 0 |
| `make test` | 4 静态检查（arch boundary / 宿主 offsets / 交叉编译 offsets / x86 stub 链接）+ 10 个 QEMU 场景 ALL PASS | 0 |
| Linux gate | 双 VM 启动 / 拓扑 / timer / VM1 SYSTEM_OFF / VM0 存活 | 通过 |

## Linux gate 证据

| 检查项 | 证据 |
|---|---|
| 双 VM 启动 | `busybox rootfs up — vm0/vm1 interactive shell` 均出现 |
| 拓扑 2×2-on-2 | `vm_list`：`0  0-1 on` / `1  0-1 on`（共享 pCPU0/1，pCPU2/3 空闲） |
| timer 在走 | VM0 arch_timer `(125,109)→(142,117)` 两列都增；VM1 `(124,125)` 两列非零 |
| VM1 SYSTEM_OFF | `poweroff -f` → `reboot: Power down` → `[hv] PSCI: VM1 SYSTEM_OFF - stopping all vCPUs` |
| VM1 停机 | 关机后 `vm_list`：`1  0-1 halted` |
| VM0 存活 | 关机后 `echo VM0_ALIVE` → `VM0_ALIVE` |

## 环境重建

本机交叉工具链前缀是 `aarch64-linux-gnu-`（非 Makefile 默认的 `aarch64-none-linux-gnu-`），
且 binutils 的 `as`/`ld` 需要 `LD_LIBRARY_PATH` 指向其宿主侧动态库目录；完整 env 见私有
memory（`qemu-toolchain-env`）。Linux `Image`、BusyBox initramfs、guest DTB 均从零构建，
步骤与 `poweroff -f` 陷阱见 [devlog](../devlog/2026-11-06-linux-gate-rebuild.md)。

## 已知限制（如实，未解决）

- **boot 挂死**（H-174 关联）：本轮 2 次 Linux 引导（冒烟 + 完整 gate）均成功，但上轮记录的
  间歇挂死（前 13 次过、第 14 次真实挂死）未消除；**2 次成功不等于它消失**。
- **timer 增量偏小**：因 VM 空闲（tickless 内核），本轮未跑上轮的「timer workload」，只验证了
  「计数非零且在走」。要对齐上轮口径需加忙等 workload 复跑。
- **6/7 位 AP 分支、RK3588 真机**未实跑（沿用上轮结论）。

## 关卡②（架构师 review，2026-11-06）

**裁定：PASS，无阻塞问题。** 已 ff-only 线性合入 `dev`（合并顶点 `0e57a20`；dev == 复验树，
三 gate 证据直接继承）。

Review 范围：`dev..feature/m11-c-survivor-fix` 共 15 提交（12 修复/测试 + 3 docs）。三个修复
（AP 上下文、状态机原子化、resident 记账）实现正确；构建一致性（-MMD/-MP、offsets 头依赖、
`-mno-outline-atomics`）正确；测试覆盖充分（host sched_test、race 50 万轮、ctxsw active-IRQ、
xvm2 stop-IPI 已入 `make test`）；ADR-0019 决定 8/9/10 与已知缺口更新到位。

非阻塞观察（不要求修复）：
- PreBits < 5 直接 fail-stop；RK3588 实跑时验证（ADR 已知缺口）。
- `tests/run_linux_xvm_test.py` 是独立 Linux gate 驱动、未接入 Makefile。
- 3 个 docs 提交（gitignore / 路线图对齐 / 复验记录）随合并一起走，无代码影响。

## 下一步

片 D 抢占（H-174 正解入口）：单独切片，先 spec + plan 走关卡①。`dev` 本地 ahead 28，
远端 `origin/develop` 推送由用户决定（本轮已确认不推送）。
