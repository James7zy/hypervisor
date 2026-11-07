# M11 片 D 时间片抢占 — 测试专家独立复验（2026-11-07）

> 分支：`feature/m11-d-preemption`，复验顶点 `63e136c`（`63e136c7f78cada2049d29f6ffa074f137abf3bc`），
> 基线 `181110c`。本报告是片 D 的**测试专家独立复验**：全部 gate 自己重跑，不采信研发自报结果；
> 另做了 4 项受控破坏实验检验测试是否承重。
> 设计：[片 D 设计](../superpowers/specs/2026-11-07-m11-slice-d-preemption-design.md)；
> 计划：[片 D 实施计划](../superpowers/plans/2026-11-07-m11-slice-d-preemption.md)；
> 决策：[ADR-0021](../adr/0021-time-slice-preemption-in-the-run-loop.md)；
> 研发受控红记录：[2026-11-07-m11-d-preempt-controlled-red.md](../debug/2026-11-07-m11-d-preempt-controlled-red.md)。

## 1. 复验范围与结论

| # | Gate | 结果 | exit | 关键原文 |
|---|---|---|---|---|
| 1 | `make host-test` | 绿 | 0 | `PASS (195 assertions)` / `scheduler PASS (34 assertions)` / `state race PASS (500000 rounds)` |
| 2 | `make test`（`make clean` 后从零） | 绿，11/11 | 0 | 4 静态检查 `PASS` + 11 场景 `ALL PASS`，`FAIL` 行 0 条，耗时 2m22s |
| 3 | `make test-qemu-preempt` ×3 | 绿 3/3 | 0,0,0 | 三次均 `ALL PASS`（3 行断言全 PASS） |
| 4 | `make test-linux` ×5 | 绿 5/5 | 0×5 | 每次 `boot/topology/console: PASS` + `Linux XVM PASS`（单次 ~10 s） |
| 5 | 负向：`LINUX_INITRD=/etc/hostname` | 立即红 | 2 | `/etc/hostname: FAILED` + `ERROR: image hash != tests/linux-gate.sha256 -- ... per docs/devlog/2026-11-06-linux-gate-rebuild.md` |
| 5b | 负向：`tests/linux-gate.sha256` 改一位 | 红 | 2 | `... /boot/Image: FAILED`、`/tmp/initramfs.cpio.gz: OK`；已还原（`git status` 干净、`diff` 同原文） |
| 5c | 负向（加做）：`LINUX_IMAGE=/tmp/does-not-exist` | 红 | 2 | `ERROR: /tmp/does-not-exist not found -- build it per docs/devlog/...` |
| 5d | 负向（加做）：`LINUX_GATE_SHA=/tmp/no-such-pin` | 红（fail-closed） | 2 | `awk: cannot open ...` + `sha256sum: 'standard input': no properly formatted checksum lines found` |

**结论：五套必需 gate 全绿，两处负向验证与两处加做负向均按预期变红；未发现阻塞问题。**
5 次 Linux 连跑全绿只是本次观测，**不构成"H-174 不会再现"的断言**（ADR-0019 明确禁止；
上一轮记录的整体口径是 14 次中 1 次真实挂死，本轮 5 次样本不足以排除）。

## 2. Gate 证据（原文摘录）

### 2.1 `make host-test`

```
build/host/host_test
PASS (195 assertions)
build/host/sched_test
scheduler PASS (34 assertions)
build/host/vcpu_state_race_test
event 6: 0 lost OFF transitions
state race PASS (500000 rounds)
EXIT=0
```

`svm_ctxsw` 切片前 `sched_test` 是 28 断言，本轮 34（新增 6 条：每入口重装量子 + 抢占分支两处）。

### 2.2 `make test`（`make clean && make test`，从零构建）

静态检查四行原文：

```
PASS: arch boundary holds
PASS: all struct offsets match assembly macros
PASS: cross-compiled struct offsets match assembly macros
（test-x86-stub：x86_64 stub 全量重编 + 链接，无输出即成功）
```

11 个 QEMU 场景顺序与原文字数：

```
basic / vtimer / traps / psci / dual / shell / vspi / ctxsw / **preempt** / xvm / xvm2
ALL PASS × 10 + VSPI ALL PASS × 1        （FAIL 行：0）
```

尾部原文：

```
PASS: 'XVM2: v0 alive after VM1 SYSTEM_OFF'
PASS: 'XVM2: v1 kept taking its own timer after VM1 SYSTEM_OFF'
PASS: 'SVM HVC: done (x1=0x0)'
ALL PASS
real 2m22.407s   EXIT=0
```

### 2.3 `make test-qemu-preempt` ×3

三次完全相同（`EXIT=0` 各一次）：

```
PASS: 'PREEMPT: v1 ran'
PASS: 'PREEMPT: v0 observed v1'
PASS: 'SVM HVC: done (x1=0x0)'
ALL PASS
```

### 2.4 `make test-linux` ×5

| 次 | exit | 尾部原文 |
|---|---|---|
| 1（`rm -rf build/test-linux` 后从零） | 0 | `survivor timers: (349, 318) -> (752, 742); console: PASS` / `Linux XVM PASS` |
| 2 | 0 | `survivor timers: (347, 319) -> (745, 724); console: PASS` / `Linux XVM PASS` |
| 3 | 0 | `survivor timers: (327, 252) -> (763, 686); console: PASS` / `Linux XVM PASS` |
| 4 | 0 | `survivor timers: (323, 216) -> (739, 641); console: PASS` / `Linux XVM PASS` |
| 5 | 0 | `survivor timers: (332, 238) -> (750, 643); console: PASS` / `Linux XVM PASS` |

哈希前置检查每次原文（第 1、3–5 次为 `.OK`，未命中的一次也没有）：

```
/home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image: OK
/tmp/initramfs.cpio.gz: OK
```

### 2.5 负向验证原文

```
# LINUX_INITRD=/etc/hostname ：整个日志只有 5 行，未触发构建与 QEMU
/home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image: OK
/etc/hostname: FAILED
sha256sum: WARNING: 1 computed checksum did NOT match
ERROR: image hash != tests/linux-gate.sha256 -- rebuild both images per docs/devlog/2026-11-06-linux-gate-rebuild.md
make: *** [Makefile:431: test-linux] Error 1          (进程 exit 2)
```

```
# tests/linux-gate.sha256 第 1 行 8b518aa -> 8b518ab
/home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image: FAILED
/tmp/initramfs.cpio.gz: OK
ERROR: image hash != tests/linux-gate.sha256 -- rebuild both images per docs/devlog/...
（还原后 diff 与原文一致，git status 干净）
```

## 3. 承重实验（受控破坏，全部已还原）

| # | 破坏方式 | 期望 | 实测 | 还原后 |
|---|---|---|---|---|
| A | `sched.c:96` 改为 `if (0 && sched_preempt_take() ...)` | host 红 | **红**：`FAIL tests/host/sched_test.c:97: resume_a_count == 2`、`scheduler FAIL (31 assertions)`，exit 2 | 34 断言 PASS，exit 0 |
| B | `cpu_arch_timer_tick_arm()` 首行 `return;` | preempt 场景红 | **红**：`PASS: 'PREEMPT: v1 ran'` / `FAIL: 'PREEMPT: v0 observed v1'` / `FAIL: 'SVM HVC: done (x1=0x0)'`，输出末尾 `PREEMPT: v1 never ran` → `done (x1=0x1)` → `PREEMPT: v1 ran` → 超时，exit 2 | 三次 `ALL PASS` |
| C | 删掉 `preempt.c` 里 vCPU0 的 `msr daifclr, #2`（guest 全程 `PSTATE.I=1` 自旋） | 若 D6 成立应仍绿 | **仍绿**（`ALL PASS`） | — |
| D | 哈希文件改一位 / 删 pin 文件 | 红且 fail-closed | 见 §2.5，均红 | 已还原 |

- **B 的实测输出与研发 `docs/debug/2026-11-07-m11-d-preempt-controlled-red.md` 记录逐行一致**
  （含"`PREEMPT: v1 ran` 在无 tick 时仍可能出现"这一细节）。研发的受控红记录属实。
- **C 是本次新做的独立实验**：设计 D6 / ADR-0021 决策 5 声称"guest 在 EL1 全程 `PSTATE.I=1`
  仍会被 EL2 tick 抢占"。`preempt.c` 本身显式 `daifclr`，所以**门禁并不覆盖这条主张**；
  删掉 vCPU0 的 `daifclr`（它从 PSCI 入口继承 `SPSR=0x3C5`，I=1）后场景仍 `ALL PASS`，
  该主张在本树、本 QEMU（9.2.4 / cortex-a72）上成立。注意这仍是 QEMU 证据，真机（M15）未跑。
- 实验 A 说明 host 用例承重的是"抢占后必须经过一次 switch-away 再回来"（`resume_a_count == 2`），
  不是"函数被调用过"。

## 4. 对抗性审查（逐项结论）

### 4.1 tick 与 `vcpu_arch_put`/`vcpu_arch_load` 的相对位置；EL2 中断掩蔽

**结论：正确，无漏抢占窗口。**

- **EL2 全程掩蔽，唯一例外就是 idle 窗口**：`grep cpu_arch_irq_enable` 在 EL2 C 里只有
  `sched.c:122` 一处，且它前后 `idle_window=1/0`（`sched.c:121-126`）。其余 EL2 C（含
  `sched_loop` 的 pick/put/load 段）都在 `PSTATE.I=1` 下跑；异常入口本身掩 DAIF；
  `hv_restore()` 回 `vm_run` 时恢复的是 `hv_save` 时的掩蔽 PSTATE。
- **顺序**：`take 旗标 → YIELD+入队 → pick → put(prev) → cur_vcpu=v → resume/load(v) →
  EV_LOAD → cpu_arch_timer_tick_arm() → vcpu_arch_run(v)`（`sched.c:96-179`）。
  `vcpu_arch_load/resume` 内部的 `el2_timer_rearm()` 读到的是**上一个 vCPU 残留的**
  `tick_deadline`，但紧接着 `tick_arm()` 无条件覆盖它并重新编程 CNTHP，且两者之间没有 eret
  （I 掩蔽），所以 guest 不可能带着残留期限跑。
- **fast path**（`sched.c:101-110`，prev 仍 RUNNING 直接重入）**不重装量子**，是 ADR-0021 决策 2
  的既定行为：避免"WFI 立即被打断的紧循环"每次重入都续期而永不被抢占。
- **idle 窗口分支**：不抢占（`irq_handler.c:149` 的 `!idle_window` 门），只消费 tick；
  此刻硬件上没有 guest 现场，`hv_restore()` 载入陈旧 HV 上下文的风险被排除。该窗口里
  `sched_idle_may_sleep()` 是 lock-free 探针，handler 里的 `sched_wake → q_lock` 也不与
  循环持锁重叠，故无死锁路径。窗口内没有 guest 在跑，因此"漏抢占"无后果——循环下一次
  进入 guest 前必然重装量子。
- 顺带确认：从 **Current-EL（SP_ELx）向量**调 `hv_restore()` 在今天的代码里仍不可达
  （preempt 分支被 `!idle_window` 挡住，idle 窗口只可能走 Current-EL），与 kick-SGI 停机路径
  的既有约束一致；将来若在别处开中断窗，需要重新审这条。

### 4.2 `preempt_req` 旗标语义

**结论：三种边界情形都正确。**

- **`prev == NULL`**：`sched_preempt_take()` 在 `&&` 最左，仍被求值，旗标被消费但无动作——
  正确，没有 RUNNING 的 vCPU 可抢。唯一副作用是一次空转的原子交换，可忽略。
- **prev 被远端 sweep 成 OFF**：`state == RUNNING` 不成立 → 不做 `vcpu_state_step`、不入队、
  不 `put`（与 sweep 契约一致：被 sweep 的 vCPU 不做硬件保存），旗标消失，循环继续 pick。
- **同一 pCPU 连发两次 tick**：实际不可达（第一次 tick 就 `hv_restore()`，不可能再 eret 回
  guest 让第二次发生）；即便发生，旗标是布尔量 → 合并成一次抢占，而"一次"正是需要的全部
  （循环随后必然重选并重装量子）。不会丢抢占、也不会欠账。
- 跨 pCPU 无共享：数组按 `current_vcpu_id()`（percpu `cpu_id`）索引，只由本 pCPU 的 timer
  IRQ 写、本 pCPU 的循环清。

### 4.3 `el2_timer_rearm()` 是否仍是唯一 CNTHP 写点

**结论：是。** `grep -rn "CNTHP_CVAL\|CNTHP_CTL" hypervisor/` 只命中
`arch/arm64/timer/vtimer.c:95,96,98`，其余全是注释/`#define`。`vtimer.h` 把这条写成不变量。

**min 语义在"tick 已过期但中断因 bg 触发"时**：handler 先 `vtimer_tick_expire()` 再
`vtimer_bg_expire()`（`irq_handler.c:145-146`）。前者的判据是 `deadline <= now`，与"CNTHP
为什么触发"无关，因此该情形下 tick 被判为到期 → 消费置 `~0ULL` → bg 唤醒到期者 → rearm
只剩 bg。**抢占与背景唤醒都不丢**。反向（bg 到期、tick 在将来）时 `tick_expire` 返回 false，
不抢占，rearm 取 `min(bg, tick) = tick`。两个方向都正确。

### 4.4 到期边界、`CNTFRQ_EL0==0`、溢出

- **比较方向**：装 `now + quantum`，判 `deadline > now` 为"未到期"，即 `deadline <= now` 到期；
  CNTHP 在 `CNTPCT >= CVAL` 触发。三者同向，没有反号/off-by-one。
- **`CNTFRQ_EL0 == 0`**：直接 return（不装 tick、不 rearm）。后果是该核永不被抢占；注释写明
  "architecturally impossible"，属防御性退化而非功能路径。可接受。
- **溢出**：`freq * SCHED_QUANTUM_MS / 1000`，`freq ≤ 1 GHz` → `1e10 < 2^64`，`now +` 亦安全。
  10 ms 折算到计数：QEMU 62.5 MHz → 625000，RK3588 24 MHz → 240000，两者整除无截断。
- 一处分母口径与 ADR 文字略有差别（ADR 写 `CNTFRQ_EL0/100`，实现写 `freq*MS/1000`）；MS=10 时
  等价，非问题。

### 4.5 host 用例是否承重

**是**（实验 A）：把抢占分支改成恒假，`make host-test` 精确红在
`tests/host/sched_test.c:97`（`resume_a_count == 2`），而不是靠某条弱断言通过。
该断言的含义也检查过：`switch_in()` 对 A 计数，B 在 `switch_in` 时被确定性 SYSTEM_OFF 扫掉，
所以 `resume_a_count == 2` 确实要求"A 被真正换下过一次再被重新选入"，fast path 自旋会把它
留在 1。另有两处增量断言同样承重：`tick_arm_count == run_count`（每次入口重装一次量子）与
`vcpu_arch_put` 里的 `state == VCPU_RUNNABLE`（换下前必须已转 RUNNABLE）。

### 4.6 `preempt.c` 是否承重

**是**（实验 B）：`cpu_arch_timer_tick_arm()` 直接 return 后场景必红，且红的原因可读
（`PREEMPT: v1 never ran` + `done (x1=0x1)`），不是静默超时。判据强弱与研发记录一致：
`PREEMPT: v1 ran` 一行不承重（无 tick 时 vCPU0 放弃后 vCPU1 仍会补打），承重的是后两行。
`canary` 检查（x19–x23）在两次非自愿切换后仍成立，说明 `put/resume` 确实重建了 GPR 现场。

### 4.7 `tests/linux-gate.sha256` 与 `Makefile` 的 `test-linux`

- **覆盖路径**：awk 用第 1/2 行的哈希分别配 `$(LINUX_IMAGE)`/`$(LINUX_INITRD)`，所以**环境变量
  覆盖后校验的是覆盖后的文件**（`/etc/hostname` 案例就是证据），pin 文件里的绝对路径只是说明。
  与 Makefile 注释一致。
- **fail-closed**：pin 文件缺失/不可读 → `sha256sum -c -` 读到空输入 → 非零退出 → 红（5d）；
  镜像缺失 → 红并指向 devlog（5c）；不匹配 → 红并 `exit 1`（5a/5b）。**没有任何一种失败会静默
  放行**。唯一残余：pin 文件被截成一行时只校验 Image，不校验 initrd——该文件在 git 里，
  风险可忽略。
- **引号/空格**：`for f in "$(LINUX_IMAGE)" ...` 与 `awk` 传给 `sha256sum` 的路径处理空格是
  **正确**的（实测 `/tmp/sp ace/Image` 输出 `OK`）；但后续
  `LINUX_IMAGE=$(LINUX_IMAGE) ... python3 ...`（`Makefile:433,435`）**没有加引号**，带空格路径会
  触发 `/bin/sh: 1: ace/Image: not found` 并以 exit 127 失败。**失败是响亮的、不会假绿**，
  但错误信息误导（指向"命令找不到"而不是"路径带空格"）。严重度低，列为非阻塞观察。
- **失败是否 `exit 1`**：是，`|| { echo ERROR...; exit 1; }`，make 打印 `Error 1`（进程 exit 2）。

### 4.8 既有 10 个场景的 expect 是否被改动

**没有**。`git diff 181110c..HEAD -- tests/svm/expect/` 只有新增 `preempt.txt`（3 行，全文见下），
其余 expect 文件零改动：

```
+PREEMPT: v1 ran
+PREEMPT: v0 observed v1
+SVM HVC: done (x1=0x0)
```

`tests/run_svm_test.sh` 未改动（仍是定长匹配、顺序无关、10 s 超时）。

### 4.9 构建告警

`make clean && make`（默认 `HV_GUEST=linux`）与 `make clean && make test`（含 svm/x86 stub 全部
profile）均 **0 条 `warning`**。`-Werror` 在全局 `CFLAGS`（`Makefile:71-75`）与 `SVM_CFLAGS`
（`Makefile:119`）里，且全树没有 `-w` / `-Wno-*` / `#pragma GCC diagnostic` 之类的绕过。
`test-qemu-preempt` 的 profile 也重编过，同样零告警。

## 5. 阻塞问题

**无。** 五套 gate 全绿，两处指定负向验证与两处加做负向均按预期变红，两处承重实验
（host 抢占分支、tick 装定）都能把对应 gate 打红，受控红与研发记录一致。

## 6. 非阻塞观察

1. **文档交付未落地（计划 T6/T7）**：`181110c..HEAD` 的 5 个提交只到 T5。当前树里
   **没有** `docs/devlog/2026-11-07-*`、**没有** `docs/reports/2026-11-07-m11-d.md`
   （受控红文档末尾"见本轮报告"目前指向不存在的文件）；`docs/adr/0021-*.md` 存在但**未跟踪、
   未提交**；ADR-0009 没有片 D 的四层 gate 修订段；ADR-0019 的"共享 pCPU 协作等待饥饿"条目
   仍是缺口态；`docs/reference/arm/current-architecture.md` 与 `AGENTS.md` 的 Current state
   段未更新。代码 gate 不受影响，但 spec DoD 4/5 未达成，关卡②前需补齐并提交。
2. **`Makefile:433,435` 的 `LINUX_IMAGE=`/`LINUX_INITRD=` 未加引号**：路径含空格时失败信息
   误导（见 4.7）。默认路径不含空格，不构成假绿。修法是一行加引号。
3. **`hypervisor/common/vm/vm_configs.c:24-27` 的 B1 注释已过期**：仍写 `svm_ctxsw` profile 的
   用例"keep DAIF masked"，而 `ctxsw.c:74`、`preempt.c:44/73` 都主动 `daifclr`。属片 B1 遗留
   注释，非片 D 引入。
4. **`preempt` 场景不覆盖"量子数值"与"公平性时延"**（研发已在受控红文档"已知边界"里写明）。
   本树也没有任何 gate 覆盖 4.3 的"tick 与 bg 共用比较器"的竞争窗口——只能靠代码审阅。
5. **D6 主张的覆盖度**：门禁场景显式 `daifclr`，所以"guest 掩中断仍被抢占"只有 devlog 探针 +
   本次实验 C 的证据，不是常态门禁。建议在 M15 真机 bring-up 时重跑（ADR-0021 已记）。
6. **单 vCPU 独占 pCPU 时仍每 10 ms 自抢占一次**（走 self re-pick 分支，无 `put`、无 TLB/FP
   重建），成本很小，但确实存在；ADR-0021 把 100 Hz 定为可接受，未列为缺陷。

## 7. 已知限制（沿用，未在本轮消除）

- `make test-linux` 的 5 次全绿**不等于** H-174 消失（ADR-0019）：上一轮记录是 14 次中 1 次
  真实挂死，本轮样本 5 次。
- Linux gate 的 `survivor timers` 只验证"计数在走"，不含忙等工作负载；timer 时延不在判据内。
- 抢占只在 QEMU 9.2.4 / `cortex-a72`（TCG）上验证；RK3588 真机（M15）未跑，含 `PSTATE.I`
  探针、20 位 CNTFRQ/hour、以及 `-mgeneral-regs-only` 下 SVM 用例的 FP 语义。
- 量子固定 10 ms、无优先级：只保证前向进度，不保证时延（H2 再评估）。
- 本次复验不覆盖 x86 侧语义：`cpu_arch_timer_tick_arm()` 在 `arch/x86/stub/hooks.c` 只是空实现，
  `test-x86-stub` 仅证明链接闭合。

## 8. 环境与镜像哈希

```
HEAD                63e136c7f78cada2049d29f6ffa074f137abf3bc  (feature/m11-d-preemption)
QEMU                QEMU emulator version 9.2.4
交叉编译器          aarch64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Host 编译器         cc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
Python              3.12.3
内核                6.8.0-111-generic，16 核
env                 PATH=/tmp/wrapbin:$PATH
                    LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib
                    CROSS_COMPILE=aarch64-linux-gnu-
```

```
8b518aa62f70675f718c96d0f24ab85b892c992fc2d7616b2051904ae89b9bce  /home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image
f4b7a485f85772a71bf0346b4d37995095b2a398c55dc28a064427876bca9c36  /tmp/initramfs.cpio.gz
```

以上两行与 `tests/linux-gate.sha256` 完全一致（`sha256sum` 实测）。工作树在复验结束时除
`docs/reports/2026-11-07-m11-d-verification.md`（本文件）与既有未跟踪文件
（`docs/adr/0021-*.md`、`scripts/plugctl.sh`、`hypervisor/arch/arm64/board/rk3588/rk3588.md`）
外干净；所有受控破坏实验均已还原并复跑为绿。

## 9. 下一步

1. 关卡②（架构师）可基于本报告与 §2 证据 review 代码；阻塞项为零。
2. 合入前补齐 §6.1 的文档交付（T6/T7）并提交，特别是 ADR-0021 需入档（当前未跟踪）、
   ADR-0009/0019 修订、devlog 与研发报告。
3. 顺手项（可选）：`Makefile:433,435` 加引号；`vm_configs.c` 的 DAIF 注释按当前用例更新。
