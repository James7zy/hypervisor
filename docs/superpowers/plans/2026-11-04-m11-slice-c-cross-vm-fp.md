# M11 片 C — 跨 VM 共享、FP/SIMD、拓扑翻转 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 两个 VM 的 vCPU 安全共享同一 pCPU：跨 VM 切换带 Stage-2/VMID、EL1 与 FP 不串 VM、vGIC/SGI 不串 VM 且不丢、离 CPU 阻塞的 vCPU 仍被自己的定时器唤醒——最终 Linux 演示拓扑翻到 2×2-on-2。

**Architecture:** 全部生长在 B2 的 put/block/load/resume 缝隙上，**不新增 arch 钩子**（spec §7：CNTVOFF/VMID/TLB/FP 都藏在 put/load 内部）。resume/load 的序固定为 Stage-2 先、EL1 后、TLB 失效最后；SGI 待决改 per-vCPU 并用 LR2+LR3 双槽；EL2 物理定时器 CNTHP（INTID 26）作为离 CPU 阻塞者的背景唤醒源，在 resume/load 末尾与其中断处理里重编程；FP 状态在独立 `.S` 中存取。

**Tech Stack:** freestanding C11 + AArch64 汇编、QEMU `virt`（`-cpu cortex-a72`，ARMv8.0）、host 单测、QEMU gate（8 → 9 场景）、手工 Linux gate（翻到 2×2-on-2）。

**Spec:** `docs/superpowers/specs/2026-10-30-m11-vcpu-scheduler-design.md` §5 片 C；ADR-0019；B2 关卡② 片 C 账单（`docs/reports/2026-10-31-day5.md`）。

> **关卡①:** 2026-11-04 APPROVE_WITH_CHANGES（R1–R8 已并入本文，本版为重写）。裁决要点：
> resume 序 CNTV → Stage-2 → EL1 → `tlbi vmalle1` → VMPIDR → vGIC 正确（VTTBR 必须先于
> EL1：推测性 EL1&0 walk 会把入 VM 的条目塞进出 VMID）；ADR-0019"片 C 不受 SGI 串扰影响"
> 判断有误，per-vCPU 待决确认必要；背景定时器重编程点 = resume/load 末尾 + CNTHP 中断，idle
> 入口不需要；PAC 降级为 TID3 特性屏蔽（HCR.APK/API=0 本就陷入密钥访问，风险是 guest 崩在
> EC 0x09，不是密钥泄漏）；vuart TX 按行缓冲不进 C，列为片 D 前置项。

## 2026-11-05 回归收尾状态（覆盖下方原任务模板的状态）

原 T1–T7 实现已按用户指示提前合入，原测试专家未通过、关卡②未执行。本轮在
`feature/m11-c-survivor-fix` 修复，不把下方历史 gate 的未执行记录改写为通过。
详见 [修复 devlog](../../devlog/2026-11-05-m11-c-regression-fix.md) 和
[报告](../../reports/2026-11-05-m11-c-regression-fix.md)。

- [x] 校准 xvm2 采样与 Linux marker/NO_HZ 判据；active-IPI 检测 RED→GREEN→回退 RED。
- [x] 补 AP0/AP1 上下文，关闭原 Task 8 的 AP known gap；补 active IRQ round-trip 检测。
- [x] 修复 LOAD 失败后的 stale prev，host 确定性测试及 Linux IABT 现场对应。
- [x] CAS 状态转换与原子 OFF 旧状态驱动 stop-kick；50 万轮 host 竞争通过。
- [x] 启动挂死定位：14 次中 1 次，跨 VM 同步等待占住彼此所需 pCPU；实际 QEMU 不陷入 WFE。
      当前范围仅定位，不实施 D；保留非 gate `wfe_wait` 诊断。公平性处理进入既定 D 设计输入，
      无条件 kick 轮转方案被否决，目标关联的新通知协议须另批。
- [x] 更新布局 pins/构建依赖；`make test` 10 场景与 host-test 通过。
- [x] 最终代码 `86e8ad3` 干净树：全量 gate/host 各两轮通过、连续 3 次 Linux gate，通过摘要已入报告。
- [ ] Linear H-27 同步（当前会话无可用集成/授权）。

正式测试专家复验、架构师关卡②和合入集成分支**不在本轮**；片 D 未开工。

## Global Constraints

- **EL2 C 代码仍 `-mgeneral-regs-only`**：FP/SIMD 指令只能出现在新 `.S` 文件里。
- **裁定 9.2**：只翻 **Linux 演示**拓扑；既有 SVM profile 不变；新增的 `svm_xvm` 是附加 profile。
- **隔离优先**：跨 VM 可见的状态（Stage-2、TLB、EL1、FP、CNTV、vGIC LR、SGI）在 fresh load 和跨 VM resume 后都不得残留上一个驻留者的值。
- 协作调度（无抢占，片 D 才有）：两个常驻 vCPU 共享 pCPU 时只会让出轮转、不会阻塞；用例与 gate 不得依赖"忙等中的 vCPU 被换下"。
- 每 Task 后 `make test` 全绿 + `make host-test`；改头文件 → `make clean`。gate 绿后才 commit。
- 合并方式 = **线性**（rebase + `--ff-only`）。分支 `feature/m11-c-cross-vm-fp`。
- 新文档中文；架构图 Mermaid；ADR 编号续 0020。

## Review Focus

| # | 失败模式（合理的人会期望的行为） | 钉住它的测试/步骤 |
|---|---|---|
| 1 | 跨 VM resume/fresh load 后 guest 访存或 EL1 状态来自**另一个 VM**（Stage-2 / TTBR / SP_EL0 / FP） | xvm：同 IPA 各写各值读回 + fresh-load 后 EL1/FP 为零 + 轮转 canary（Task 5） |
| 2 | VM0 的 IPI 投进 VM1，或两个 SGI 同时待决时丢一个（Linux csd_lock 挂起） | xvm SGI 段：VM1 计数恒 0、VM0 两个 INTID 都收到（Task 5）；手工 gate SMP 正常（Task 7） |
| 3 | 离 CPU 阻塞的 vCPU 自己的 timer 到期却永不醒 | xvm 定时器段：确定性挂起即失败（Task 4 桩掉时的受控红在案）（Task 5） |
| 4 | 一个 VM SYSTEM_OFF 把共享 pCPU 停死，另一个 VM 卡住 | xvm 尾段 VM1 关机后 VM0 仍 PASS + 手工 gate VM1 poweroff（Task 5/7） |
| 5 | resume 后 `spi_shadow_pending` 残留 → 控制台按键丢失/重放 | vgic_restore 在锁内清旗标 + 手工 gate 双 shell 交互（Task 1/7） |

---

### Task 1: 跨 VM 切换序 + 影子旗标

**Files:**
- Modify: `hypervisor/arch/arm64/guest/vm.c`
  - `vcpu_arch_resume` 序：CNTV → `stage2_activate(v)` → `el1_restore` → `tlbi vmalle1; dsb nsh; isb` → VMPIDR → `vgic_restore`。注释写明 VTTBR 先于 EL1 的理由（推测性 EL1&0 walk）。
  - `vcpu_arch_load` 序：`vgic_init` → VMPIDR → `stage2_activate` → EL1（Task 1b 的清零+恢复）→ `isb` → `tlbi vmalle1; ic iallu; dsb nsh; isb` → CNTV → `vgic_restore`。现在的 tlbi 在 `stage2_activate` 之前，刷的是**出** VMID。
- Modify: `hypervisor/arch/arm64/vgic/vgic.c`（`vgic_restore` 在 `spi_lock` 内读 LR1 影子、写活 LR1、清 `spi_shadow_pending`；三者同锁才无竞态）
- Modify: ADR-0019 "恢复序"条目补 Stage-2 位置。

- [ ] Step 1：实现；`make test` 8× 全绿（同 VM 下 stage2_activate 写回相同 VTTBR，行为不变）；commit。

### Task 1b: EL1 保存集补全 + fresh load 清零（关卡① R1/R7）

**Files:**
- Modify: `hypervisor/arch/arm64/include/arch/vm.h`（`struct el1_state` 追加 `sp_el0`、`elr_el1`、`spsr_el1`、`tpidr_el0`、`par_el1`、`cntkctl_el1`；`check_offsets_target.c` 重钉 `el1` 大小与被推移的尾偏移）
- Modify: `hypervisor/arch/arm64/guest/vm.c`
  - `el1_save/el1_restore` 加上这 6 项（SP_EL0 是 Linux 的 `current`；CNTKCTL 是 Linux 写的 EVNTEN/EL0VCTEN——halflist 文档里它的触发条件"EL2 背景定时器重设计"在 Task 4 发生）。
  - `vcpu_arch_load`：先把 `v->arch.el1` 整块清零，再调用 `el1_restore`，取代现在孤立的 `SCTLR=0`——fresh boot 得到全零 EL1（SCTLR=0 的 warm-reset 语义保留），不再继承上一个驻留者的 TTBR/VBAR/TPIDR 等。
  - `vcpu_arch_load` 同时把 CNTV 影子复位为 `ctl=IMASK, cval=0`（关卡① A3：避免 re-ON 恢复一个已过期且未掩蔽的期限，顺带关闭 ADR-0019 的 N6 缺口）。
- Modify: `tests/svm/cases/ctxsw.c` + expect（SP_EL0、TPIDR_EL0 全值 canary——SVM guest 运行在 EL1h，这两个寄存器是惰性的，最强哨兵）
- Modify: `docs/debug/m11-b2-el1-halflist.md` 对照表（补 6 项，CNTKCTL 从缺口移入已保存）

- [ ] Step 1：实现 + 偏移重钉；ctxsw ALL PASS；`make test` 8× 全绿；手工 Linux gate 12/12（1:1 下 hotplug re-ON 走新的清零 load）；commit。

### Task 2: per-vCPU SGI 待决 + LR2/LR3 双槽（关卡① R3）

**Files:**
- Modify: `hypervisor/arch/arm64/include/arch/vm.h`（`vcpu_arch` 尾部 `u16 sgi_pending;` + 偏移重钉）
- Modify: `hypervisor/arch/arm64/vgic/vgic_sgi.c`
  - 删 `sgi_pending[NR_CPUS]`；`vgic_sgi_trap` 在 `sgi_lock` 下把位写进**目标 vCPU**：目标即发送者 → 立即 drain；目标在别的 pCPU 上运行 → kick 其亲和 pCPU；目标 BLOCKED → `sched_wake`；目标在队列中 RUNNABLE → 只记位，resume 时 drain。
  - `vgic_sgi_drain_vcpu(struct vcpu *v)`：只注入能放进**空闲** LR 的位（LR2、LR3 中 state 位 [63:62] 为 0 者），**只清已注入的位**，其余留在 `arch.sgi_pending`；仍有剩余时~~置 `ICH_HCR_EL2.NPIE`~~ **（实现偏离，2026-11-04：改为占用槽置 LR EOI 位——NPIE 在两槽皆 active-only 时电平持续而无可注入；见 ADR-0019）**，由维护中断（INTID 25）在 LR 空出时重试。kick 分支的 `vgic_sgi_drain(cpu)` 改为对 `current_vcpu()` 调用它。
- Modify: `hypervisor/arch/arm64/vgic/vgic.c`（`vgic_inject_sgi` 取目标 LR 参数，不再固定覆盖 LR2；`vgic_init` 清 `sgi_pending`）
- Modify: `hypervisor/arch/arm64/irq/irq_handler.c`（维护中断分支：INTID 25 → `vgic_sgi_drain_vcpu(current_vcpu())`，完全 deactivate；GIC 侧使能 PPI 25，与 PPI 27 同处）
- Modify: `hypervisor/arch/arm64/guest/vm.c`（resume/load 在 `vgic_restore` 之后 drain；自重拾路径也 drain——关卡① A5）
- Modify: `hypervisor/arch/arm64/vmexit/vmexit.c`（WFI block 后的再检查增加 `|| v->arch.sgi_pending`，与 `spi_shadow_pending` 对称）
- Modify: `hypervisor/common/vm/vm_configs.c` 注释、ADR-0019（缺口标记"片 C 修复"，并订正误判）

- [ ] Step 1：实现；`make test` 8× 全绿（psci 的 SGI 唤醒段为回归网）；手工 Linux gate 12/12（1:1 下 Linux SMP 的 IPI 全走新路径——RESCHEDULE 与 CALL_FUNC 同时待决的情形由真实负载覆盖）；commit。

### Task 3: FP/SIMD 急切保存（关卡① R5）

**Files:**
- Create: `hypervisor/arch/arm64/guest/fp.S`（`fp_save(struct fp_state *)` / `fp_restore(const struct fp_state *)`：`stp q0,q1,[x0,#0]` … `stp q30,q31,[x0,#480]`；FPSR/FPCR 用 `str`/`ldr` 写在 #512/#520——`stp x,x,[x0,#512]` 越出偏移范围 −512..504，实测汇编失败）
- Create: `hypervisor/arch/arm64/guest/fp.h`（arch 私有原型）
- Modify: `hypervisor/arch/arm64/include/arch/vm.h`（`struct fp_state { u64 q[64]; u64 fpsr; u64 fpcr; } __attribute__((aligned(16)));` 追加到 `vcpu_arch` 尾部；`struct vcpu` 对齐随之变为 16，重钉尾偏移；钉 `sizeof(struct fp_state) == 528` 与 `offsetof(struct fp_state, fpsr) == 512`，把 `.S` 里的常量绑到结构体）
- Modify: `hypervisor/arch/arm64/Makefile`（arch-objs 加 `guest/fp.o`）
- Modify: `hypervisor/arch/arm64/guest/vm.c`（put 和 block 都 `fp_save`——从阻塞者切走不经过 put；resume/load `fp_restore`；load 前把 FP 影子清零）
- Modify: `hypervisor/arch/arm64/cpu/cpu.c` + `cpu/secondary.c`（写入完整定义值 `CPTR_EL2 = 0x33FF`：E2H=0 下的 RES1 位，TFP/TTA/TAM/TCPAC 全为 0；不做读-改-写——复位值架构上 UNKNOWN。今天 CPTR_EL2 从未写过）
- Modify: `Makefile`（ctxsw 与 xvm 两个用例的 guest 编译加 `-mgeneral-regs-only`：FP canary 只有在编译器保证不碰 v 寄存器时才可信；盖章用内联汇编，实测在该 flag 下可编译）
- Modify: `tests/svm/cases/ctxsw.c` + expect（FP 段：两 vCPU 各在 v0–v31 写互异值，轮转后各自校验。**FP 段必须放在 CPACR 盖章之后**——0xE1D00000/0xF1D00000 的 FPEN=01 允许 EL1 FP；QEMU 复位 CPACR 为 0，首条 FP 指令会陷入）

- [ ] Step 1：实现 + 偏移钉；`make test` 8× 全绿；ctxsw FP 段 ALL PASS；commit。

### Task 4: EL2 背景定时器（关卡① R4）

**Files:**
- Modify: `hypervisor/arch/arm64/timer/vtimer.c`（`void vtimer_bg_arm(u32 pcpu)`：扫描**亲和本 pCPU、BLOCKED、且不是当前 vCPU** 的 vCPU，取影子中 `ENABLE=1 && IMASK=0` 者的最早物理期限 `cntv_cval + cntvoff`（CNTVCT = CNTPCT − CNTVOFF），编程 `CNTHP_CVAL_EL2` + `CNTHP_CTL_EL2=1`；集合为空则 `CNTHP_CTL_EL2=0`。影子只在本 pCPU 写，扫描无需加锁）
- Modify: `hypervisor/arch/arm64/guest/vm.c`（`vcpu_arch_resume` 与 `vcpu_arch_load` 末尾调用 `vtimer_bg_arm`——需要覆盖的集合只在"切入别的 vCPU"时增长。**不新增通用层钩子**，sched_loop 不动）
- Modify: `hypervisor/arch/arm64/irq/irq_handler.c`（`intid == BOARD_HYP_TIMER_IRQ`：对每个期限已过的离 CPU 阻塞 vCPU 调 `sched_wake`；然后重编程 CNTHP（或置 CTL=0）+ `isb`——电平触发，**先重编程再** priority-drop 与 deactivate；不 HW-forward）
- Modify: `hypervisor/arch/arm64/board/qemu_virt/board.h`（`BOARD_HYP_TIMER_IRQ 26`）；`irq/gic_v3.c` 的 `gic_init` 与 `cpu/secondary.c` 各使能 PPI 26，Group 1 NS，优先级 0xA0，与 PPI 27 同处
- ADR-0019：共享自 timer 唤醒缺口标记"片 C 修复"；记录 per-pCPU 单 CNTHP 取最早期限的选型；片 D 抢占 tick 复用同一定时器（v8.0 无 CNTHV），届时取 min(tick, 最早期限)

- [ ] Step 1：实现；`make test` 8× 全绿（1:1 下永无离 CPU 阻塞者 → CNTHP 恒关）；commit（场景验证在 Task 5）。

### Task 5: 跨 VM 共享隔离场景 `xvm`（聚合 8 → 9，关卡① R6 重塑）

**Files:**
- Modify: `Makefile`（profile `svm_xvm`：`-DCONFIG_GUEST_SVM=1 -DCONFIG_NR_VMS=2 -DCONFIG_SVM_XVM=1`；目标 `test-qemu-xvm` 进聚合；guest 按 vspi 先例同源编两份 `-DXVM_ID=0/1`，带 `-mgeneral-regs-only`）
- Modify: `hypervisor/common/vm/vm_configs.c`（`CONFIG_SVM_XVM`：VM0 `vcpu_pcpu = {0, 1}`、VM1 `vcpu_pcpu = {2, 0}`——VM0 vCPU0 与 VM1 vCPU1 共享 pCPU0）
- Create: `tests/svm/cases/xvm.c`、`tests/svm/expect/xvm.txt`
- 时钟：两 VM 共享 CNTVCT（cntvoff=0），以绝对计数为跨 VM 时间轴。
- 流程（**只有 VM0 vCPU0 与 VM1 vCPU1 打印**；VM0 vCPU1 经 VM0 内存汇报——两个 pCPU 同时打印会交织）：
  1. VM0 vCPU0 arm 期限 D0（≈ +200 ms），独处 wfi → **阻塞**。
  2. ≈ +50 ms，pCPU2 上的 VM1 vCPU0 CPU_ON VM1 vCPU1 到 pCPU0（**fresh load**）；VM1 vCPU1 断言其 EL1（含 SP_EL0/TPIDR）与 FP 寄存器全为零（fresh-load 泄漏 canary），然后开 vGIC、解 DAIF，**忙等不 wfi** 到 ≈ +400 ms。
  3. D0 在忙等期间到期 → CNTHP 唤醒 VM0 vCPU0（入队，暂不能运行——协作调度）。
  4. ≈ +300 ms，pCPU1 上的 VM0 vCPU1 向 VM0 vCPU0 发 SGI 5 和 SGI 6（此时 pCPU0 当前是 VM1 vCPU1）；VM1 vCPU1 的 SGI 计数必须保持 0。
  5. VM1 vCPU1 wfi → 让出给 VM0 vCPU0；VM0 vCPU0 收到自己的 timer 中断，并收到 SGI 5 **和** 6（双槽 + 保留位）。
  6. 轮转阶段：两者在同一 IPA 写 `0xA000 + XVM_ID`、盖 EL1 与 FP canary，8 轮后各自校验均为本 VM 的值。
  7. 尾段：VM1 vCPU0 发 SYSTEM_OFF，此时 VM1 vCPU1 在 pCPU0 上运行（被 kick）；VM0 vCPU0 随后必须仍能打印 PASS（钉住 Task 6 的 R2 修复）。
- **受控红（Q9）**：工作树内把 `vtimer_bg_arm` 桩掉 → 第 1–5 步确定性挂起（VM1 vCPU1 让出后 VM0 vCPU0 不在队列）→ 记 `docs/debug/` → 还原。同理可在工作树内回退 Task 2 观察 VM1 计数非零。

- [ ] Step 1：profile + 用例 + 目标；`make test-qemu-xvm` ALL PASS；受控红记档。
- [ ] Step 2：`make test` **9 场景**全绿；commit。

### Task 6: kick 停机改解栈 + PAC 特性屏蔽（关卡① R2/R8）

**Files:**
- Modify: `hypervisor/arch/arm64/irq/irq_handler.c`（kick 分支 `cv->state == VCPU_OFF` 且不在 idle 窗口时：由 `cpu_arch_halt()` 改为 `hv_restore()`——与 `psci_system_off_vm` 同一解栈，循环看到 prev 为 OFF 后 pick 下一个；共享 pCPU 上另一个 VM 的 vCPU 不再被困死）。更新 irq_handler.c 与 vpsci.c 中"idles forever"一类注释。
- Modify: `hypervisor/arch/arm64/vmexit/vmexit.c`（TID3 转发器屏蔽 `ID_AA64ISAR1_EL1` 的 APA/API/GPA/GPI 与 `ID_AA64ISAR2_EL1` 的 APA3/GPA3 字段为 0——guest 永不启用 PAuth，HINT 空间的 PAC 指令保持 NOP。HCR.APK/API=0 已陷入密钥访问，风险是 guest 崩在 EC 0x09，不是密钥泄漏）
- ADR-0019：PAC 条目改写为"TID3 特性屏蔽；真正的密钥存取触发条件 = 首个要求 guest 使用 PAuth 的目标"，订正原理由。

- [ ] Step 1：实现；`make test` 全绿；commit（R2 的场景验证在 Task 5 尾段与 Task 7）。

### Task 7: Linux 演示拓扑翻到 2×2-on-2 + 手工 gate

**Files:**
- Modify: `hypervisor/common/vm/vm_configs.c`（Linux 表 VM1 `vcpu_pcpu = {2, 3}` → `{0, 1}`，引裁定 9.2；pCPU2/3 此后空闲，保留 `NR_CPUS=4` 与 `-smp 4`）
- Modify: `/tmp/m11a-gate.sh`（不入库）：`vm_list` VM1 行期望 `0-1`；新增断言——两个 VM 都能 `cat /proc/interrupts`，且 **cpu1 那一列** arch_timer 计数在走（关卡① A1：pCPU0 上两个 vCPU0 永远在让出轮转、不会阻塞，背景定时器只在 pCPU1 上承重）。gate 内**不得忙等**（协作调度下会饿死同 pCPU 的另一个 VM）。
- Modify: `docs/reference/arm/current-architecture.md`（拓扑描述）

- [ ] Step 1：翻转 + 脚本更新；`make test` 9 场景全绿。
- [ ] Step 2：手工 Linux gate 全过（双 shell、Ctrl-T、cpu1 hotplug 往返、VM1 关机后 VM0 存活、两个 VM 的 cpu1 timer 计数都在走）；输出摘录进日报；commit。

### Task 8: ADR-0020 + 收尾

- [ ] ADR-0020《FP/SIMD 急切保存先于惰性》：每次换下/阻塞存 528 B，换入恢复；否决惰性 `CPTR_EL2.TFP` 陷阱法（触发条件 = RK3588 实测切换开销）；含 Mermaid。
- [ ] ADR-0019 修订汇总：Task 1/1b/2/4/6 的订正；新增已知缺口（各带触发条件）——~~`ICH_AP1R0_EL2` 未保存（触发：片 D 抢占，A2）~~（11-05 已补 AP0/AP1；原触发判断被 SYSTEM_OFF stop IPI 证伪）；PPI 27 物理屏蔽是 per-pCPU 状态被共享 vCPU 共用（A3 余项）；EL1 物理定时器 `CNTP_*` 未保存且 `CNTHCTL.EL1PCEN=1`（触发：首个使用 CNTP 的 guest，A4）；RK3588 A76/A55 推测性 AT 勘误（1165522/1530923，触发：RK3588 bring-up，A6）；AMAIR/AFSR0/1/CSSELR 未保存（A8）；pCPU0 永久让出轮转的负载形态（A1）。
- [ ] 里程碑骨架片 C 勾选；本 plan 状态行；devlog + 日报；Linear H-27 追加。

---

## 不在本片

- **vuart TX 按行缓冲**：列为片 D（`make test-linux` 自动化）前置项。原因订正：2×2-on-2 下 VM0（pCPU1）与 VM1（pCPU0）仍会同时打印，交织并不减少；它是测试稳定性债，不是隔离问题。手工 gate 继续用单行横幅纪律。
- `CPTR_EL2.TTA=1`（陷入 trace 寄存器，关卡① A7）：与 MDCR 策略一致，但本片写入的 `0x33FF` 保持 TTA=0；记为后续可选项。

## 执行方式

主会话 executing-plans；测试专家独立验收 + 架构师关卡②后线性合并。粗估 4–6 loop 日（关卡①新增 Task 1b 与 SGI 双槽）。
