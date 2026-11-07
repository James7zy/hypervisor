# M11 片 D — 抢占时间片与 Linux 门禁自动化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给共享 pCPU 上的 vCPU 一段固定量子（10 ms），到期无条件换下；把 H-174 的确定性
复现（`wfe_wait` 同构件）做成 gate 场景；`make test-linux` 一条命令跑通双 VM Linux 门禁。

**Architecture:** tick 复用已有 CNTHP（PPI 26），`el2_timer_rearm()` 是唯一写点，取
`min(背景 vtimer 最早期限, tick)`。中断上下文只置 per-pCPU 旗标并 `hv_restore()` 解栈；
状态转换与入队留在 `sched_loop`（`VCPU_EV_YIELD` + `sched_yield_cur`）。通用层只多一个
arch 钩子 `cpu_arch_timer_tick_arm()` 和 `sched_preempt_request()/take()`。

**Tech Stack:** freestanding C11 + AArch64 asm、QEMU `virt`（`-cpu cortex-a72`）、host 单测、
`make test`（10 → 11 场景）、`make test-linux`。

**Spec:** [2026-11-07-m11-slice-d-preemption-design.md](../specs/2026-11-07-m11-slice-d-preemption-design.md)。
前置事实已由架构师实验钉住：guest 全程 `PSTATE.I=1` 时 EL2 tick **仍投递**（QEMU 9.2.4）；
基线 `make host-test` + `make test` 10/10 绿（2026-11-07，`dev` @ `1b3f426`）。

## Global Constraints

- **EL2 C 仍 `-mgeneral-regs-only`**；新 asm 不需要（`hv_restore` 现成）。
- **`struct percpu` / `PERCPU_SIZE` / `check_offsets` 不动**（tick 状态归 arch timer 模块私有数组）。
- **CNTHP 只有一个写点**：`el2_timer_rearm()`。任何新代码直接写 `CNTHP_CVAL/CTL` 即为 bug。
- 中断上下文只做：消 deadline、唤醒到期者、重编程、EOI（drop+deactivate）、置旗标、解栈。
  **不做状态转换、不入队、不切上下文**。
- 每 Task 后 `make host-test` + `make test` 全绿；改头文件 → `make clean`。
- 分支 `feature/m11-d-preemption`，线性合回 `dev`（rebase + `--ff-only`）。
- 新文档中文；架构图 Mermaid；ADR 续号 0021。
- 环境（本机）：`export PATH=/tmp/wrapbin:$PATH
  LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib CROSS_COMPILE=aarch64-linux-gnu-`。

## Review Focus

| # | 失败模式（合理的人会期望的行为） | 钉住它的测试/步骤 |
|---|---|---|
| 1 | 到期 tick 换下 A 后 A 再也没被选中（饿死） | host sched_test：抢占后 A 入队且被再次选中（T2） |
| 2 | 抢占把 guest 寄存器/FP/EL1 状态丢掉或串 vCPU | `preempt` 场景 guest 校验自己的标记；`make test` 既有 10 场景回归（T3/T4） |
| 3 | idle 窗口里 `hv_restore()` 载入陈旧 HV 上下文 → pCPU 死 | idle 窗口分支显式不抢占；`make test` 内含 CPU_OFF/parking 场景（T3） |
| 4 | 切 vCPU 时 `vtimer_bg_arm` 把 tick 抹掉 → 抢占静默失效 | T1 收敛为单一写点；`preempt` 场景在真实切换下仍绿（T4） |
| 5 | tick 风暴（中断里不先重编程就 EOI） | 电平线先重编程再 drop+deactivate；`make test` 不出现 tick 打印洪水（T3） |

## Task 1: CNTHP 单一写点（不改变行为）

**目的:** 让背景 vtimer 与未来的 tick 共用一个比较器，先把"谁写 CNTHP"收成一处。

- `hypervisor/arch/arm64/timer/vtimer.c`
  - 新增 `static u64 tick_deadline[NR_CPUS];`，`vtimer_init()` 里全部置 `~0ULL`（幂等）。
  - `vtimer_bg_arm()` → 改名 `el2_timer_rearm()`，内部 `next = min(bg_earliest, tick_deadline[current_vcpu_id()])`；
    `next == ~0ULL` 时 `CNTHP_CTL_EL2=0`，否则写 CVAL/CTL。
  - `vtimer_bg_expire()` 末尾调用改名后的函数。
- `hypervisor/arch/arm64/timer/vtimer.h`：声明改名 + 注释（唯一写点）。
- `hypervisor/arch/arm64/guest/vm.c:126,244`：两处 `vtimer_bg_arm()` → `el2_timer_rearm()`。
- 验证：`grep -rn "CNTHP_CVAL\|CNTHP_CTL" hypervisor/` 只剩 `vtimer.c` 一处；`make test` 10/10。

## Task 2: 抢占消费路径（host RED → GREEN）

**RED commit（仅测试）:**
- `tests/host/sched_test.c` 新增用例：`vcpu_arch_run(A)` 里调 `sched_preempt_request()` 后返回
  （模拟 tick 解栈），断言下一轮 `vcpu_arch_run` 的目标不是 A，且 A 之后仍被选中（不饿死）；
  `NR_CPUS=1` 时 A 被重新选中并重新装量子（`cpu_arch_timer_tick_arm` 计数 +1）。
- 该 commit 必须**链接失败**（`sched_preempt_request` 未定义）——按 Day-3 裁定 1 的 RED 形态。

**GREEN:**
- `hypervisor/include/sched.h`：`#define SCHED_QUANTUM_MS 10`；
  `void sched_preempt_request(void); bool sched_preempt_take(void);`
- `hypervisor/common/sched/sched.c`：
  - `static u32 preempt_req[NR_CPUS];`
  - `sched_preempt_request()`：`__atomic_store_n(&preempt_req[current_vcpu_id()], 1u, __ATOMIC_RELEASE)`。
  - `sched_preempt_take()`：`__atomic_exchange_n(..., 0, __ATOMIC_ACQ_REL) != 0`。
  - `sched_loop()` 顶部（fast path 之前）：
    ```c
    if (sched_preempt_take() && prev != NULL &&
        __atomic_load_n(&prev->state, __ATOMIC_ACQUIRE) == VCPU_RUNNING) {
        if (vcpu_state_step(&prev->state, VCPU_EV_YIELD) == 0)
            sched_yield_cur();      /* 入本 pCPU 队列，循环随后重选 */
    }
    ```
  - 底部 `vcpu_arch_run(v);` 之前加 `cpu_arch_timer_tick_arm();`
    （只覆盖 switch-in / self-re-pick；fast path 继承原 deadline）。
- `hypervisor/include/cpu.h`：`void cpu_arch_timer_tick_arm(void);` + 一句注释（x86 语义：
  VMX preemption timer / LAPIC timer，ADR-0015 可移植性表已列）。
- `hypervisor/arch/x86/stub/hooks.c`：空实现（否则 `make test-x86-stub` 链接失败——这正是它的用途）。
- `tests/host/sched_test.c`：补 `void cpu_arch_timer_tick_arm(void) {}` 的 host 假实现。
- 验证：`make host-test` 三套全绿（断言数增加）。

## Task 3: ARM64 tick 装定与 IRQ 路径

- `hypervisor/arch/arm64/timer/vtimer.c`
  ```c
  void cpu_arch_timer_tick_arm(void)
  {
      u64 f, now;
      __asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(f));
      __asm__ volatile("isb");
      __asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(now));
      if (f == 0) return;                       /* 不可能；防御读数为 0 时除以 0 */
      tick_deadline[current_vcpu_id()] = now + f / (1000 / SCHED_QUANTUM_MS);
      el2_timer_rearm();
  }
  bool vtimer_tick_expire(void)   /* 到期 → 消 deadline 返回 true */
  ```
- `hypervisor/arch/arm64/irq/irq_handler.c` 的 `BOARD_HYP_TIMER_IRQ` 分支：
  ```c
  bool tick = vtimer_tick_expire();
  vtimer_bg_expire();                 /* 唤醒到期者 + 重编程 min（tick 已消或仍在） */
  gic_priority_drop(intid);
  gic_deactivate(intid);
  if (tick && !cpu_arch_this_percpu()->idle_window) {
      sched_preempt_request();
      hv_restore();                   /* noreturn：run loop 里切换 */
  }
  ```
  注意顺序：先消条件/重编程再 EOI（电平线）；`idle_window` 时只返回。
- 验证：`make test` 10/10 仍绿；手工一次 `make run`（Linux 镜像临时设 `LED`? 不需要）——
  只要求上面 gate。

## Task 4: `preempt` gate 场景（受控红 → 绿）

- `tests/svm/cases/preempt.c`（新）：`svm_ctxsw` profile，VM0 两 vCPU 共享 pCPU0。
  - 两 vCPU 开头 `msr daifclr, #2`。
  - vCPU1（CPU_ON 进来的 secondary）：**不 WFI、不让出**，自旋若干轮后写 `volatile`
    标记并 `svm_puts("PREEMPT: v1 ran\n")`。
  - vCPU0：`CPU_ON(1)` 后自旋等标记（同样不 WFI），看到后校验自己的 canary 未被破坏，
    `svm_puts("PREEMPT: v0 observed v1\n")`，`svm_done(0)`。
- `tests/svm/expect/preempt.txt`：上面两行 + `SVM HVC: done (x1=0x0)`。
- `Makefile`：`SVM_CASES += preempt`（或独立 pattern 调用，取实现更小者），新增
  `test-qemu-preempt`，加进 `test` 聚合（10 → 11）。注释写明"无 tick 时超时"。
- **受控红留证**：临时把 `cpu_arch_timer_tick_arm()` 体改成 `return;` 跑一次 →
  记录超时输出到 `docs/debug/2026-11-07-m11-d-preempt-controlled-red.md`，还原后跑绿。
- 决定 `wfe_wait.c` 去向：`preempt` 已覆盖同缺口时，把 `wfe_wait.c` 标注为"被 preempt 场景
  取代"保留为诊断，不重复进 gate（写一行注释 + devlog 说明）。
- 验证：`make test` 11/11；连跑 3 次 `test-qemu-preempt` 稳定。

## Task 5: `make test-linux` + 哈希钉住

- `tests/linux-gate.sha256`（新，两行 `sha256sum` 格式）：
  - 由本机镜像生成：`sha256sum $LINUX_IMAGE $LINUX_INITRD`（当前值见报告）。
- `Makefile`：
  ```make
  LINUX_IMAGE  ?= $(HOME)/Music/virtual/linux-6.12.103/arch/arm64/boot/Image
  LINUX_INITRD ?= /tmp/initramfs.cpio.gz
  test-linux:
          # 存在性 + sha256 校验（不匹配即 error，文本指向 devlog 构建步骤）
          # 然后：$(MAKE) BUILD_DIR=build/test-linux all
          #       LINUX_IMAGE=... LINUX_INITRD=... python3 tests/run_linux_xvm_test.py
  ```
  校验用一个 shell 片段（`sha256sum -c`），不新增脚本文件。
- `tests/run_linux_xvm_test.py` 不动（已有 `--self-test`；`test-linux` 里先跑 self-test，秒级）。
- 验证：干净树 `make test-linux` 一次跑到 `Linux XVM PASS`，exit 0；故意改坏一个哈希
  → 红且错误文本清楚。

## Task 6: ADR 与文档

- **ADR-0021**（新，中文）：时间片抢占的切换点与量子。内容：为什么在 run loop 消费而非
  中断上下文切换（状态机单一入口、`hv_restore` 解栈语义）；idle 窗口不抢占；量子 10 ms；
  guest 掩中断已实验证不影响投递；被否方案（无条件 kick 轮转 —— 片 C 否决理由；
  远程 IPI 轮转；guest 内计时）。
- **ADR-0009 修订**（追加一段带日期的 amendment）：gate 现在是四层——build /
  `make test`（4 静态 + 11 QEMU 场景）/ `make test-linux`（双 VM Linux）/ host 单测层；
  "no automated suite" 前提已过期，ADR 保留为 gate 定义。
- **ADR-0019**：「共享 pCPU 的协作等待饥饿」条目改为"片 D 修复（tick 抢占）+ 验证边界"，
  引用 `preempt` 场景与 Linux 连跑次数；补 `tick 与背景 vtimer 共用比较器` 一行。
- `docs/reference/arm/current-architecture.md`：调度/定时器/钩子清单三处。
- `AGENTS.md` Current state 段（slices A/B1/B2/C/D）。

## Task 7: 整理、受控实验记录与收尾

- `docs/debug/2026-11-07-m11-d-preempt-controlled-red.md`：受控红 + 掩中断探针实验结果
  （探针代码形态与输出，代码不留仓库）。
- `docs/devlog/2026-11-07-m11-d-preemption.md`：特性说明（命令、结构图、证据）。
- `docs/reports/2026-11-07-m11-d.md`：切片、commit 列表、三套 gate 原文摘录、架构师关卡②、
  已知缺口、Linear 待办（用户暂停同步）。
- M11 [实施计划](2026-10-30-m11-vcpu-scheduler.md) 片 D DoD 勾选 + commit 哈希。
- 线性合回 `dev`（rebase + `--ff-only`），删除切片分支。

## 不在本片

- 目标关联通知协议、权重/优先级调度、迁移与负载均衡。
- 下载式镜像获取（裁定 9.1 的落地见 spec D8）。
- RK3588 真机验证（M15）。

## 执行方式

研发 subagent 按 T1→T7 顺序实现（每 Task 后跑 gate，红则原地修，计入 2-strike）；
测试专家 subagent 独立跑三套 gate + 对抗性审查；架构师（主会话）做关卡② review 并合入。
