# M11 片 C — 跨 VM 共享、FP/SIMD、拓扑翻转 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 两个 VM 的 vCPU 安全共享同一 pCPU：跨 VM 切换带 Stage-2/VMID、vGIC/SGI 不串 VM、阻塞 vCPU 的定时器在离 CPU 时仍能唤醒、FP/SIMD 急切保存——最终 Linux 演示拓扑翻到 2×2-on-2（两个 VM 各一 vCPU 共享 pCPU0/1）。

**Architecture:** 全部生长在 B2 的 put/block/load/resume 缝隙上。resume 补 Stage-2 激活并把 `tlbi` 挪到 VTTBR 之后（对**入**VMID 失效）；SGI 待决从 per-pCPU 位图改 per-vCPU，入口处合并进 LR；EL2 物理定时器（CNTHP，PPI 26）作为"离 CPU 阻塞 vCPU 的最早 CNTV 期限"的背景唤醒源；FP 状态在独立 `.S` 里存取（EL2 C 的 `-mgeneral-regs-only` 不解除）。

**Tech Stack:** freestanding C11 + AArch64 汇编、QEMU `virt`（`-cpu cortex-a72`，ARMv8.0）、host 单测、QEMU gate（8 场景 → 9）、手工 Linux gate（翻到 2×2-on-2）。

**Spec:** `docs/superpowers/specs/2026-10-30-m11-vcpu-scheduler-design.md` §5 片 C；ADR-0019（缺口与触发条件）；B2 关卡② 片 C 账单（`docs/reports/2026-10-31-day5.md`）。

## Global Constraints

- **EL2 C 代码仍 `-mgeneral-regs-only`**（08-10 Q5 维持）：FP 指令只能出现在新 `.S` 文件里。
- **裁定 9.2**：只翻 **Linux 演示**拓扑；SVM 测试 profile（basic/dual/shell/vspi/psci/ctxsw…）不变。新的跨 VM 共享 SVM profile 是新增，不替换。
- **隔离优先**：任何跨 VM 可见的状态（Stage-2、TLB、vGIC LR、SGI、FP、EL1、CNTV）切换后不得残留上一个 VM 的值——宁可多刷不可漏刷。
- 每 Task 后 `make test` 全绿（新场景落地前 8 场景，之后 9）+ `make host-test`；改头文件 → `make clean`。
- gate 绿后才 commit；合并方式 = **线性**（rebase + `--ff-only`，2026-11-04 用户指示）。
- 用例打印纪律：同一 pCPU 上的两个 vCPU 只在 wfi 时切换，一行内不会交织；跨 pCPU 的并发打印用"静默旗标"模式（B2 先例）。
- 新文档中文；架构图 Mermaid；ADR 编号续 0020。

## Review Focus

| # | 失败模式（一个合理的人会期望的行为） | 钉住它的测试/步骤 |
|---|---|---|
| 1 | 跨 VM resume 后 guest 访存走了**另一个 VM 的 Stage-2**（读到对方内存） | xvm 场景：两 VM 在同一 IPA 各写各值、交错切换后各自读回（Task 5） |
| 2 | VM0 的 IPI 投进 VM1（pCPU 共享时 per-pCPU 位图串 VM） | xvm 场景：VM0 自发 SGI 只能由 VM0 收到，VM1 计数为 0（Task 5） |
| 3 | 离 CPU 阻塞的 vCPU 自己的 timer 到期却永不醒（Linux idle 挂起） | xvm 场景：VM1 arm timer 后 wfi 阻塞、VM0 持续占用 pCPU，VM1 仍按期被唤醒（Task 5）+ 2×2-on-2 手工 gate（Task 7） |
| 4 | FP 寄存器跨 VM 泄漏/腐坏 | ctxsw FP 盖章段 + xvm 双 VM 各自 FP 盖章（Task 3/5） |
| 5 | resume 后 `spi_shadow_pending` 残留 → 下次 put 保留陈旧 LR1 影子（控制台按键丢失/重放） | vgic_restore 清旗标 + 2×2-on-2 手工 gate 双 shell 交互（Task 1/7） |

---

### Task 1: 跨 VM 切换的 Stage-2 + 序审计 + 影子旗标清除

**Files:**
- Modify: `hypervisor/arch/arm64/guest/vm.c`（`vcpu_arch_resume`：在 `el1_restore` **之前**调用 `stage2_activate(v)`（`VTTBR_EL2`/VTCR 换成本 vCPU 所属 VM 的），再 `tlbi vmalle1`——`vmalle1` 作用于**当前** VMID，必须在 VTTBR 切换后执行才失效**入** VM 的条目；`vcpu_arch_load`：把现有 `SCTLR=0; tlbi vmalle1; ic iallu` 移到 `stage2_activate` 之后（现在的序刷的是**出** VMID））
- Modify: `hypervisor/arch/arm64/vgic/vgic.c`（`vgic_restore` 在 `spi_lock` 内写 LR1 并清 `spi_shadow_pending`：影子已被装进活寄存器，此后活寄存器才是权威）
- Modify: `hypervisor/arch/arm64/guest/vm.c` 注释（resume/load 的新序与理由）、`docs/adr/0019-two-layer-state-division.md`（"恢复序"条目补 Stage-2 位置；**订正**"共享 profile 跨 vCPU SGI 缺口——片 C 不受影响"的错误判断：2×2-on-2 下 pCPU 跨 VM 共享，按 pCPU 键控会串 VM，由 Task 2 修复）

**Interfaces:**
- Produces：resume 的固定序 = CNTV → **stage2_activate** → el1_restore → `tlbi vmalle1` + `dsb nsh; isb` → VMPIDR → vgic_restore（清旗标）。load 同理（Stage-2 先于 tlbi）。

- [ ] Step 1：实现序调整 + 旗标清除；`make test` 8× 全绿（1:1 与同 VM 共享下行为不变：同 VM 的 stage2_activate 写回相同 VTTBR）。
- [ ] Step 2：commit。

### Task 2: per-vCPU SGI 待决（跨 VM 隔离阻塞项）

**Files:**
- Modify: `hypervisor/arch/arm64/include/arch/vm.h`（`struct vcpu_arch` 尾部追加 `u16 sgi_pending;`；`check_offsets_target.c` 钉新偏移，并重钉被推移的通用尾偏移）
- Modify: `hypervisor/arch/arm64/vgic/vgic_sgi.c`（删 `sgi_pending[NR_CPUS]`；`vgic_sgi_trap` 把位写进**目标 vCPU** 的 `arch.sgi_pending`（锁沿用 `sgi_lock`）；目标正在本 pCPU 运行（`target == current_vcpu()`）→ 立即 drain 进 LR2；在别处运行 → kick 其亲和 pCPU；不在任何 CPU 上 → 只记位 + `sched_wake(target)`；`vgic_sgi_drain(cpu)` 改为只排空 **`current_vcpu()` 自己的**位）
- Modify: `hypervisor/arch/arm64/guest/vm.c`（`vcpu_arch_resume`/`vcpu_arch_load` 在 `vgic_restore` 之后调用 `vgic_sgi_drain` 的 per-vCPU 版本，把离 CPU 期间累积的 SGI 装进 LR2）
- Modify: `hypervisor/common/vm/vm_configs.c`（删去共享 profile 注释里"sgi_pending 按 pCPU 键控"的已知缺口说明）、ADR-0019（缺口条目标记"已于片 C 修复"）

**Interfaces:**
- Produces：`void vgic_sgi_drain_vcpu(struct vcpu *v)`（只排空 v 自己的待决；调用方保证 v 是本 pCPU 当前 vCPU）。旧 `vgic_sgi_drain(u32 cpu)` 保留为 `vgic_sgi_drain_vcpu(current_vcpu())` 的包装，供 kick 分支调用。

- [ ] Step 1：实现；`make test` 8× 全绿（psci 的 SGI 唤醒段 + Linux 1:1 IPI 路径为回归网）。
- [ ] Step 2：手工 Linux gate 12/12（1:1 下 Linux SMP 的 IPI 全走新路径）；commit。

### Task 3: FP/SIMD 急切保存

**Files:**
- Create: `hypervisor/arch/arm64/guest/fp.S`（`fp_save(struct fp_state *)` / `fp_restore(const struct fp_state *)`：`stp q0,q1 … q30,q31` + `mrs fpsr/fpcr`，对称恢复；该文件不带 `-mgeneral-regs-only`——`.S` 本就不受 C flag 约束）
- Modify: `hypervisor/arch/arm64/include/arch/vm.h`（`struct fp_state { u64 q[64]; u64 fpsr; u64 fpcr; } __attribute__((aligned(16)));`——C 侧只把 Q 寄存器当 64 个 u64 存储（`-mgeneral-regs-only` 下不引入向量类型），16 字节对齐保证 `.S` 里 `stp q` 合法；追加到 `vcpu_arch` 尾部；`check_offsets_target.c` 钉偏移与 `sizeof == 528`）
- Modify: `hypervisor/arch/arm64/Makefile`（arch-objs 加 `guest/fp.o`）
- Modify: `hypervisor/arch/arm64/guest/vm.c`（put/block：`fp_save`；resume/load：`fp_restore`；reset：FP 影子清零——fresh boot 不得看到上一个驻留者的 FP 值）
- Verify：`CPTR_EL2.TFP == 0`（EL2 自己执行 FP 指令所需；grep 现有 CPTR 设置，若未显式设置则在 `cpu_arch_init` 与 secondary 初始化里显式清 TFP，并注释）
- Modify: `tests/svm/cases/ctxsw.c` + expect（FP 盖章段：两 vCPU 各在 v0–v31 写互异值，轮转后各自校验；guest 侧 FP 用内联汇编，SVM 用例编译是否带 `-mgeneral-regs-only` 先核对 Makefile 的 `SVM_CFLAGS`）

**Interfaces:**
- Produces：`struct fp_state`、`fp_save/fp_restore`（C 原型在 arch 私有头 `guest/fp.h`）。

- [ ] Step 1：实现 + 偏移钉；`make test` 8× 全绿（1:1 行为不变）。
- [ ] Step 2：ctxsw FP 段 ALL PASS；commit。

### Task 4: EL2 背景定时器——离 CPU 阻塞 vCPU 的唤醒源（ADR-0019 缺口触发）

**Files:**
- Modify: `hypervisor/arch/arm64/timer/vtimer.c`（新 `void vtimer_bg_arm(u32 pcpu)`：扫描**亲和本 pCPU、状态 BLOCKED、且不是当前 vCPU** 的 vCPU，取其影子里 `CTL.ENABLE=1 && !IMASK` 的最早物理期限（`cval + cntvoff`），编程 `CNTHP_CVAL_EL2` + `CNTHP_CTL_EL2=1`；无则 `CNTHP_CTL_EL2=0`）
- Modify: `hypervisor/arch/arm64/irq/irq_handler.c`（新分支 `intid == BOARD_HYP_TIMER_IRQ`（PPI 26）：对所有期限已过的离 CPU 阻塞 vCPU 调 `sched_wake`，再 `vtimer_bg_arm` 重编程；物理中断完全 deactivate——不 HW-forward）
- Modify: `hypervisor/arch/arm64/board/qemu_virt/board.h`（`BOARD_HYP_TIMER_IRQ 26`）、`cpu/secondary.c` + `irq/gic_v3.c` 的 per-CPU PPI 使能（与 PPI 27、kick SGI 同处）
- Modify: `hypervisor/common/sched/sched.c` 的 `sched_loop`（**切换后**与**进入 idle 前**调用 `vtimer_bg_arm(pcpu)`——前者覆盖"阻塞者被换下"，后者覆盖"阻塞者独处时其活 timer 已在本 CPU 上，背景定时器多余但无害"）——需要一个 arch 钩子 `cpu_arch_bg_timer_rearm(u32 pcpu)`（通用层不直接调 vtimer，ADR-0015），x86 stub 同 commit
- ADR-0019：共享自 timer 唤醒缺口标记"已于片 C 修复"，记录选型（per-pCPU 单个 CNTHP，最早期限；与片 D 抢占 tick 共用同一定时器——届时取 min(tick, 最早期限)）

**Interfaces:**
- Produces：`cpu_arch_bg_timer_rearm(u32 pcpu)`（arch 钩子）；片 D 复用 CNTHP 时在同一函数里并入抢占 tick。

- [ ] Step 1：实现；`make test` 8× 全绿（1:1 下永无离 CPU 阻塞者 → CNTHP 恒关，行为不变）。
- [ ] Step 2：commit（场景验证在 Task 5）。

### Task 5: 跨 VM 共享隔离场景 `xvm`（聚合 8 → 9）

**Files:**
- Modify: `Makefile`（新 HV_GUEST profile `svm_xvm`：`-DCONFIG_GUEST_SVM=1 -DCONFIG_NR_VMS=2 -DCONFIG_SVM_XVM=1`；新目标 `test-qemu-xvm` 进 `test` 聚合；guest 二进制按 vspi 先例同一源编两份 `-DXVM_ID=0/1`）
- Modify: `hypervisor/common/vm/vm_configs.c`（`CONFIG_SVM_XVM`：VM0 `vcpu_pcpu = {0, 1}`、VM1 `vcpu_pcpu = {0, 3}`——两 VM 的 vCPU0 共享 pCPU0；注释写明这是跨 VM 共享的测试 profile，裁定 9.2 不受影响）
- Create: `tests/svm/cases/xvm.c`、`tests/svm/expect/xvm.txt`
- 用例（两 VM 同一源，按 `XVM_ID` 分支）：
  1. 在同一 IPA 的数据页写 `0xA000 + XVM_ID`，盖 EL1 惰性 canary（TPIDR/CONTEXTIDR）与 FP canary（v8 = XVM_ID 相关值）；
  2. 轮转 8 轮（打印 `XVM<id>: r<n>` 定长标记 + wfi 让出）；
  3. 校验：内存值、EL1 canary、FP canary 均为**本 VM 的值**；
  4. SGI 隔离：VM0 向**自己**发 SGI 5（target = Aff0 0），VM0 handler 计数 1；VM1 的 SGI 计数必须保持 0；
  5. 背景定时器：VM1 arm 一个较远期限后 wfi **阻塞**（VM0 此时 busy-loop 打印占住 pCPU0，不 wfi），VM1 必须按期被唤醒并打印 `XVM1: own timer woke me while off-CPU`；
  6. 各自打印 PASS 行；VM0 等 VM1 的完成旗标——两 VM 内存隔离，旗标经**各自打印的行**由 expect 文件判定（不共享内存）。
- expect：每 VM 的定长 PASS 行 + `[hv] PSCI`/banner 行。

- [ ] Step 1：profile + 用例 + 目标；`make test-qemu-xvm` ALL PASS。
- [ ] Step 2：`make test` **9 场景**全绿；commit。

### Task 6: PAC 守卫（不实现密钥保存）

**Files:**
- Modify: `hypervisor/arch/arm64/cpu/cpu.c`（`cpu_arch_init` 读 `ID_AA64ISAR1_EL1` 的 APA/API/GPA/GPI 字段；任一非零 → BUG 停机，打印"PAuth 已实现但密钥不在保存集——跨 VM 共享会泄漏密钥"；QEMU `cortex-a72`（v8.0）与目标 RK3588（A76/A55，v8.2）都不实现 PAuth，守卫在两者上都是空操作）
- ADR-0019：PAC 缺口条目由"共享 Linux 前必须补"改为"以启动守卫代替；首个实现 PAuth 的目标核出现时补存取（触发条件）"——**提请关卡①裁定**这一降级

- [ ] Step 1：守卫 + 文档；`make test` 全绿；commit。

### Task 7: Linux 演示拓扑翻到 2×2-on-2 + 手工 gate

**Files:**
- Modify: `hypervisor/common/vm/vm_configs.c`（Linux 表：VM1 `vcpu_pcpu = {2, 3}` → `{0, 1}`；注释引裁定 9.2；pCPU2/3 此后空闲——保留 `NR_CPUS=4` 与 `-smp 4`，不改 board）
- Modify: `/tmp/m11a-gate.sh`（gate 脚本不入库）期望行：`vm_list` 的 VM1 行从 `2-3` 改为 `0-1`；新增断言：VM0 与 VM1 都能 `cat /proc/interrupts` 且 arch_timer 计数在走（证明共享 pCPU 上两个 Linux 的 timer 都活着——Review Focus #3 的真实载荷）
- 文档：`docs/reference/arm/current-architecture.md` 的拓扑描述更新为 2×2-on-2

- [ ] Step 1：翻转 + 脚本更新；`make test` 9 场景全绿（SVM profile 不受影响）。
- [ ] Step 2：手工 Linux gate 全过（双 shell、Ctrl-T、cpu1 hotplug 往返、VM1 关机隔离、两个 VM 的 timer 计数都在走）；输出摘录进日报；commit。

### Task 8: ADR-0020 + 收尾

- [ ] ADR-0020《FP/SIMD 急切保存先于惰性》：Decision = 每次换下/阻塞存 528 B、换入恢复；否决惰性 `CPTR_EL2.TFP` 陷阱法（首次 FP 使用陷入 + 记账，QEMU 无收益，真机实测后再议，触发条件 = RK3588 切换开销测量）；含 Mermaid。
- [ ] ADR-0019 修订记录汇总（Task 1/2/4/6 的四处订正）。
- [ ] 里程碑骨架片 C 勾选；本 plan 状态行；devlog + 日报；Linear H-27 追加。

---

## 候选项（不在本片范围，提请关卡①裁定是否并入）

- **vuart TX 按行缓冲**：两个 VM 从不同 pCPU 同时输出时，物理串口上字符级交织（Linux 启动横幅 flake 的根因，B2 手工 gate 重试过一次）。修法：vuart 按 VM 缓冲到 `\n` 再在 console 锁下整行刷出。属测试稳定性债，2×2-on-2 下两个 VM 改为同 pCPU 轮转、交织概率反而下降——建议**不并入**，记为片 D（`make test-linux` 自动化）的前置项。

## 执行方式

主会话 executing-plans；测试专家独立验收 + 架构师关卡② 后**线性合并**回 develop。分支：`feature/m11-c-cross-vm-fp`。粗估 3–5 loop 日。
