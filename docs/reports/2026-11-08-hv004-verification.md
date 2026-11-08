# HV-004（第一刀）VM 暂停/恢复 — 测试专家独立复验（2026-11-08）

> 分支：`feature/hv004-vm-pause`，复验顶点 `e9c434cf53ff5c075191553acb9df92c618c744d`，
> 基线 `ac844e4`（`dev`）。本报告是 HV-004 第一刀的**测试专家独立复验**：五套必需 gate 全部自己重跑，
> 不采信研发/架构师自报；另做 4 项受控破坏实验检验测试是否承重，并用临时 `printk` 探针确认 kick
> 分支在真实运行中确实被走到（探针已还原）。**未 commit、未 push、未新建分支。**
>
> 设计：[HV-004 设计](../superpowers/specs/2026-11-08-hv004-vm-pause-design.md)（F1–F6）；
> 计划：[HV-004 实施计划](../superpowers/plans/2026-11-08-hv004-vm-pause.md)（T1–T3 + Review Focus）；
> 决策：[ADR-0023](../adr/0023-vm-pause-is-a-scheduling-predicate.md)（工作区未跟踪，见 §5-N4）。
>
> **本轮背景**：实现子会话在 T3 收尾时超时，最后两个提交（`8047413` 场景、`e9c434c` ON_PENDING
> 修复）由架构师接手完成。所以 §4 对这两处的时序编排与 `ON_PENDING` 覆盖做了额外怀疑性检查。

## 1. 复验范围与结论

| # | Gate | 结果 | exit | 关键原文 |
|---|---|---|---|---|
| 1 | `make host-test` | 绿 | 0 | `PASS (195 assertions)` / `scheduler PASS (60 assertions)` / `state race PASS (500000 rounds)` / `hypercall PASS (57 assertions)` |
| 2 | `make clean && make test`（从零） | 绿，12/12 场景 + 4 项静态检查 | 0（clean 0） | `PASS: arch boundary holds` / `PASS: all struct offsets match assembly macros` / `PASS: cross-compiled struct offsets match assembly macros` / 场景 `ALL PASS`×11 + `VSPI ALL PASS`×1；`PASS:` 145 行，`FAIL:` 0 行 |
| 3 | `make test-qemu-hcall` ×6 | 绿 6/6 | 0×6 | 每次 17 行断言全 `PASS:` + `ALL PASS`，末行 `PASS: 'HCALL: vm1 observed pause'` |
| 4 | `make test-linux` ×2 | 绿 2/2 | 0×2 | `parser self-test PASS` / `boot/topology/console: PASS` / `Linux XVM PASS` |
| 5a | `make test-x86-stub`（`rm -rf build/test-x86` 后全新重建） | 绿 | 0 | 链接行含 `build/test-x86/obj/common/hypercall/hypercall.o`、`.../common/sched/sched.o`；`warning:`/`error:` 0 条 |
| 5b | `make check-arch-boundary`（独立再跑） | 绿 | 0 | `PASS: arch boundary holds` |
| 5c | `make check-offsets` | 绿 | 0 | `PASS: all struct offsets match assembly macros` |
| 5d | `make check-offsets-target` | 绿 | 0 | `PASS: cross-compiled struct offsets match assembly macros` |
| 6 | 4 项受控破坏 + 探针，全部还原后复跑 `host-test` + `test-qemu-hcall` | 绿 | 0,0 | `scheduler PASS (60 assertions)` / `hypercall PASS (57 assertions)` / `ALL PASS` |

**结论：五套必需 gate 全绿，12 个既有场景零回归；暂停/恢复的四条语义（pick 丢弃、恢复重入队、
表项 roles、重入队 ON_PENDING）在 host 侧全部承重。未发现阻塞问题。**

但有 **1 项"gate 证明力"问题必须写进记录**：`irq_handler.c` 的 kick 分支**没有任何 gate 能区分
有/无**（§3 实验 B）——删掉它 hcall 场景依然全绿，因为 10 ms 量子 tick 单独也能把暂停的 vCPU 换下。
kick 是延迟优化（10 ms → µs），不是正确性前提；代码本身经探针确认**在真实运行中被走到**。

## 2. Gate 证据（原文摘录）

### 2.1 `make host-test`（exit 0）

```
build/host/host_test
PASS (195 assertions)
build/host/sched_test
scheduler PASS (60 assertions)
build/host/vcpu_state_race_test
state race PASS (500000 rounds)
build/host/hypercall_test
hypercall PASS (57 assertions)
```

对比基线（HV-005 复验记录）：`sched_test` 34 → 60 断言（+26），`hypercall_test` 35 → 57（+22）。

### 2.2 `make clean && make test`（exit 0）

```
PASS: arch boundary holds
PASS: all struct offsets match assembly macros
PASS: cross-compiled struct offsets match assembly macros
...
PASS: 'HCALL: vm1 observed pause'
ALL PASS
```

统计：`PASS:` 145 行（HV-005 时为 138 行，+7 = `hcall` 新增 7 行 expect），`FAIL:` 0 行，
`warning:` 0 行，`error:` 0 行；`ALL PASS` 11 次 + `VSPI ALL PASS` 1 次 = 12 场景。

### 2.3 `make test-qemu-hcall` ×6（exit 0 ×6）

```
run 1: exit=0 elapsed=10s fails=0 passes=17
run 2: exit=0 elapsed=10s fails=0 passes=17
run 3: exit=0 elapsed=10s fails=0 passes=17
run 4: exit=0 elapsed=10s fails=0 passes=17
run 5: exit=0 elapsed=10s fails=0 passes=17
run 6: exit=0 elapsed=10s fails=0 passes=17
```

每次尾部：

```
PASS: 'SVM HVC: done (x1=0x100)'
PASS: 'HCALL: vm1 pause peer rc=-2'
PASS: 'HCALL: vm0 pause rc=0'
PASS: 'HCALL: vm0 peer paused=1 online=1'
PASS: 'HCALL: vm0 resume rc=0'
PASS: 'HCALL: vm0 peer paused after resume=0'
PASS: 'HCALL: vm0 repeat resume rc=-5'
PASS: 'HCALL: vm1 observed pause'
ALL PASS
```

`elapsed=10s` 是 `tests/run_svm_test.sh` 的 `TIMEOUT=10` 兜底（QEMU 不自退），非挂死。

### 2.4 `make test-linux` ×2（exit 0 ×2）

```
/home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image: OK
/tmp/initramfs.cpio.gz: OK
parser self-test PASS
boot/topology/console: PASS
survivor timers: (325, 309) -> (722, 743); console: PASS
Linux XVM PASS
```

第二次 `survivor timers: (324, 211) -> (729, 623)`。暂停语义未影响正常调度与双 VM 时间片轮转。

### 2.5 静态检查（各自独立跑，exit 0）

```
$ make test-x86-stub            # rm -rf build/test-x86 之后
x86_64-linux-gnu-objcopy -O binary build/test-x86/hypervisor.elf build/test-x86/hypervisor.bin
0            # warning:/error: 计数
$ make check-arch-boundary
PASS: arch boundary holds
$ make check-offsets
PASS: all struct offsets match assembly macros
$ make check-offsets-target
PASS: cross-compiled struct offsets match assembly macros
```

## 3. 承重实验（受控破坏，全部已 `git checkout` 还原）

每次实验后 `git status --porcelain` 只剩两条**非本轮**的未跟踪项
（`docs/adr/0023-*.md`、`scripts/plugctl.sh`），确认还原干净。

### 实验 A：删掉 `sched_loop` 里的 `vm_is_paused` 丢弃

`sched.c` 删掉 `if (vm_is_paused(v->owner)) continue;`（2 行）。

| gate | 结果 | 原文 |
|---|---|---|
| `make host-test` | **红** exit 2 | `FAIL tests/host/sched_test.c:131: resume_a_count == 1` / `:133: ... == VCPU_RUNNABLE` / `:140: sched_pick(0) == &vm[1].vcpu[0]` / `scheduler FAIL (63 assertions)` |
| `make test-qemu-hcall` | **红** exit 2 | `FAIL: 'SVM HVC: done (x1=0x100)' not found` / `FAIL: 'HCALL: vm1 observed pause' not found` |

两套都红 ✔。副作用证据：`done (x1=0x100)` 变缺失（实际打成 `0x101`，`fails`=1 进了 payload），
说明 verdict 通道确实把 guest 侧断言失败传了回去。

### 实验 B：删掉 `irq_handler.c` 的 kick 强制换下分支（4 行）

| gate | 结果 | 原文 |
|---|---|---|
| `make test-qemu-hcall` | **绿** exit 0 | `PASS: 'HCALL: vm1 observed pause'` / `ALL PASS` |

**与任务书预期（"hcall 应红，VM1 观察不到冻结空洞"）不一致**：删掉后 VM1 依然观察到 2 s 空洞。
原因是**量子 tick 自己就能换下**暂停的 vCPU：

- `BOARD_HYP_TIMER_IRQ`（EL2 `CNTHP`）在每次 guest 进入前由 `cpu_arch_timer_tick_arm()` 重新武装，
  guest 无法屏蔽；到期即 `sched_preempt_request()` + `hv_restore()`，`sched_loop` 顶部消费旗标 →
  YIELD → 入队 → pick → `vm_is_paused` → 丢弃。
- 所以暂停的生效延迟本来就 ≤ 一个量子（10 ms），kick 只把它压到 µs 级；2 s 的冻结窗口对两者
  完全一样，`CNTVCT` 空洞检测分辨不出来。

**这不是缺陷，而是"gate 证明力"的边界**：ADR-0023 的 Considered Options 已写明"tick 已经给了
有限延迟，kick 让暂停不必等满一个量子"——与实测一致。但计划 T2 的验证标准
（"`make test` 12 场景仍绿（此刻还没有场景触发暂停，属编译面）"）与 Review Focus #1
（把"目标还在跑"分配给 `hcall` 场景）**高估了 hcall 对 kick 分支的约束力**：hcall 只证明
"暂停最终生效"，不证明"kick 生效"。

**kick 分支不是死代码**：临时在分支内加 `printk("[probe] kick-evict pCPU%u\n", ...)` 后跑一次
（已还原）：

```
HCALL: vm0 pause rc=0
[probe] kick-evict pCPU2          <- 恰好 1 次，紧跟暂停，落在目标 pCPU
HCALL: vm0 resume rc=0
```

即：分支被走到、`idle_window` 门没有把它整体跳过、目标 pCPU 判断正确。

### 实验 C：`sched_vm_resume` 不接受 `ON_PENDING`

`sched.c`：`if (st != VCPU_RUNNABLE && st != VCPU_ON_PENDING)` → `if (st != VCPU_RUNNABLE)`。

| gate | 结果 | 原文 |
|---|---|---|
| `make host-test` | **红** exit 2 | `FAIL tests/host/sched_test.c:162: sched_pick(0) == &vm[1].vcpu[0]` / `scheduler FAIL (60 assertions)` |

正好打在新加的 ON_PENDING 断言上 ✔。`make test-qemu-hcall` 在此实验下**不会红**（该场景的 guest
从不 CPU_ON 二级 vCPU，暂停窗口里没有 ON_PENDING），所以 `e9c434c` 的修复**只有 host 单点覆盖，
没有端到端覆盖**（见 §5-N2）。

### 实验 D（加做）：表项 `roles` 改回 `HC_ROLE_ANY`

`hypercall.c`：`HC_VM_PAUSE` 表项 `HC_ROLE_SERVICE` → `HC_ROLE_ANY`。

| gate | 结果 | 原文 |
|---|---|---|
| `make host-test` | **红** exit 2 | `FAIL tests/host/hypercall_test.c:135: hypercall_dispatch(guest, HC_VM_PAUSE, 1, 0, &ret) == HC_EPERM` / `:137: ...(guest, HC_VM_PAUSE, 0, ...)` / `hypercall FAIL (57 assertions)` |

权限确实由**表项**承担，不是处理函数内的检查 ✔（代码侧也已确认：`hc_vm_pause/hc_vm_resume`
里没有任何 service 判断）。

### 还原后复跑（exit 0,0）

```
scheduler PASS (60 assertions)
hypercall PASS (57 assertions)
PASS: 'HCALL: vm1 observed pause'
ALL PASS
```

## 4. 对抗性审查（逐项结论）

### 4.1 时序编排是否可靠（guest 场景）

直接从一次真实运行里取毫秒时间戳（临时管道给每行加 `date`，不改代码；`t=0` 取
`[hv] pCPU2 online` 那一行）：

| t(ms) | 事件 |
|---|---|
| +0 | pCPU1/3/2 entering guest（两 VM 的 vCPU0 间隔 ≤2 ms 启动） |
| +90 | VM0：`version`/`self`/`query peer`/`unused` 四行 |
| +790 | VM1：`version`/`self`/`query peer`/`unused` + `pause peer rc=-2` |
| +2490 | VM0：`pause rc=0`、`peer paused=1 online=1` |
| +4490 | VM0：`resume rc=0`、`peer paused after resume=0`、`repeat resume rc=-5` |
| +4494 | `SVM HVC: done (x1=0x0)` |
| +4990 | VM1：`observed pause` |
| +4992 | `SVM HVC: done (x1=0x100)` |

裕量核算：

| 约束 | 实测 | 裕量 |
|---|---|---|
| VM1 空洞检测窗口（起点 +793，窗宽 5 s → 关窗 +5793）必须覆盖冻结 | 冻结 +2490…+4490，+4490 检出 | **1.30 s** |
| 空洞阈值 > 1 s（`c - last > freq`） | 冻结 2 s | **1 倍** |
| 打印互不交错：VM0 首段(+90) vs VM1 首段(+790) | 相隔 700 ms（打印耗时 µs 级） | 很大 |
| VM1 `observed pause`(+4990) vs VM0 post-resume 段结束(+4494) | 相隔 496 ms | 496 ms |
| 场景必须在 `TIMEOUT=10` 内出最后一行 | 最后一行 ≈ +5.0 s（进程启动到首行另加 <0.5 s） | **≈4.5 s** |
| 6 次连跑一致性 | 6/6 全绿，每次 17 行全 PASS | — |

结论：编排的**时序假设是"两 VM 的 guest entry 相差 ≪1.7 s"**，实测相差 2 ms，且两 VM 的 vCPU0
静态亲和在不同 pCPU（`vm_configs`：VM0=0-1、VM1=2-3），不存在互相抢占导致的对齐漂移。
`observed pause` 只在检出后打印，**暂停没生效就不打印 → hcall 必红**（实验 A 已证）。
这个 5 s/2 s/500 ms 的组合是可靠的，不是"概率上没遇到"。

两点残余脆弱性（非阻塞，已在 §7 记录）：

1. 编排完全依赖**固定绝对偏移**（100/800/2400/2000/500 ms），任何新增的 hypervisor `printk`
   落在 guest 行中间就会打断匹配（`run_svm_test.sh` 是逐行 grep）。实验 B 的探针实测**没有**
   打断任何行（探针行自成一行），但这是运气，不是保证。
2. VM1 的空洞检测分辨不了"暂停"与"其它 >1 s 的饥饿"。本拓扑下 pCPU2 只跑 VM1 的 vCPU0，
   替代解释不存在；若将来亲和改成共享 pCPU，这条证明会退化。

### 4.2 竞态：暂停 × `pick`

**安全的，且是确定性论证而非概率**：

- 丢弃点在 `sched_loop` 的 `v = sched_pick(pcpu)` **之后**，丢弃只是 `continue`，**不从队列里再删**
  （vCPU 已在 `pick` 时出队）。所以 resume 的 `sched_enqueue` 与丢弃之间没有"谁删了谁"的问题。
- 丢弃后 vCPU 的可能状态只有 `RUNNABLE` 与 `ON_PENDING` 两种（`pick` 出队前已在队列里）；
  `resume` 恰好覆盖这两个值 → **不存在"被丢在队列外且 resume 不管"的状态**。
- 若 `resume` 的 `sched_enqueue` 落在 `pick` 出队与 `is_paused` 读取之间：`vm_resume()` 先清标志，
  `pick` 侧读到的就是 false，不会丢弃。
- 若 `pick` 侧读到 true 并丢弃，则 `vm_resume()` 一定还没执行完 → `resume` 的扫描一定在丢弃之后
  → 一定重新入队。

### 4.3 竞态：暂停 × `CPU_ON`（`ON_PENDING`）

**这是真实窗口，`e9c434c` 的修复是必需的**：

- `psci` 的 `VCPU_EV_CPU_ON` 把状态置 `ON_PENDING` 并 `sched_enqueue` + kick；
  `pick` 出队后、`VCPU_EV_PICK`（`ON_PENDING → RUNNABLE`）之前，若 VM 被暂停，
  `sched_loop` 在状态检查**之前**就丢弃了它 → vCPU 停在 `ON_PENDING`、不在队列里。
- `vcpu_state.c` 里离开 `ON_PENDING` 只有 `VCPU_EV_PICK` 一条边，而 `PICK` 只在
  `sched_loop` 的暂停检查之后发生 → **没有 resume 就永远出不来**，二级 vCPU 永不上线
  （guest 的 CPU_ON 已返回成功，此后只会等）。
- 修复后的闭合性：`resume` 对 `ON_PENDING` 也入队 + kick；`pick` 随后 `ON_PENDING →(PICK)→
  RUNNABLE` 正常上线。host 断言（实验 C）精确钉住这条。
- **覆盖强度：只有 host 单点**。当前所有 QEMU 场景在暂停窗口内都没有 CPU_ON，所以
  `make test` 打不出这个洞。这是一个明确记录的覆盖缺口，不是缺陷。

### 4.4 竞态：暂停 × `wake`（BLOCKED 被唤醒后被丢弃）

**安全，且是设计内行为**（ADR-0023 Consequences 已记录"多一次队列往返"）：

- `sched_wake` 只从 `BLOCKED` 走 `WAKE → RUNNABLE` 并 `sched_enqueue` + kick；
- 若此时 VM 已暂停，`pick` 会丢弃它，但状态仍是 `RUNNABLE` → `resume` 会把它放回队列；
- `resume` 只入队 `RUNNABLE/ON_PENDING`，`BLOCKED` 不动 → **不会伪造唤醒**（F3），
  `sched_test` 有专门断言（`VCPU_BLOCKED` 保持且 `sched_pick(0) == NULL`）。

### 4.5 竞态：暂停 × 正在 `load` 的 vCPU（状态还是 `RUNNABLE`、已出队）

暂停的扫描只看 `VCPU_RUNNING`，因此会漏掉"已出队、正在 `vcpu_arch_load`"的 vCPU（此刻它还是
`RUNNABLE`）。后果：该 vCPU 会进入 guest 并跑到量子到期（≤10 ms）才被 tick 换下。
**属于"异步生效 ≤ 一个量子"的既有边界**，不是丢调度。

### 4.6 恢复语义：idle 窗口 / 队列满

- **pCPU 在 idle 窗口**：`sched_vm_resume` 先 `sched_enqueue` 再 `cpu_arch_kick`（与 `sched_wake`
  同序）。kick 在 idle 窗口经 current-EL 向量进入，`idle_window==1` 时分支 `return`（不
  `hv_restore`，无 guest 帧）；返回后 `sched_loop` 关中断、`idle_window=0`、`continue` → `pick`
  拿到已入队的 vCPU。**不会停机等死**。
- **队列满（`SCHED_Q_MAX`）**：结构上不可达。`sched.h` 有
  `_Static_assert(NR_VMS * VCPUS_PER_VM <= SCHED_Q_MAX)`，而所有 profile 的 `NR_VMS ∈ {1,2}`、
  `VCPUS_PER_VM == 2` → 每 pCPU 最多 2 项，容量 8；`sched_q_enqueue` 又对同指针幂等。
  因此 `(void)sched_q_enqueue(...)` 忽略返回值在今天是安全的（与既有 `sched_yield_cur`/
  `sched_enqueue` 同一写法）。
- **恢复一个全 OFF 的 VM**：`resume` 什么都不入队，只清标志 → 无害。
- **恢复 vs 远端 sweep 成 OFF**：`resume` 只入队 `RUNNABLE/ON_PENDING`，`OFF` 不会被放回队列；
  且被暂停的 VM 自己不可能发起 `SYSTEM_OFF`（它没在跑），所以"暂停中被 sweep"不可达。
  反向"先 sweep 再 resume"只会得到 "标志已清、无 vCPU 可跑"，与 sweep 语义一致。

### 4.7 kick 分支：位置、idle 门、`cv == NULL`

- **位置正确**：`OFF`/`cv==NULL` 判断之后、`vgic_sgi_drain_vcpu()` 之前。置于 OFF 之后是必须的
  （sweep 分支已经 `hv_restore`，否则两处都会解栈）；置于 SGI 排空之前保证被强制换下的 vCPU
  不会把本 pCPU 的 SGI 状态写进 live LR。
- **idle 窗口确实不 `hv_restore`**：`&& !cpu_arch_this_percpu()->idle_window` 门，与量子 tick
  分支同形；实测探针只在非 idle 上下文命中（§3 实验 B）。
- **`cv == NULL` 在该分支不可达**：`cpu_arch_halt()`（`cpu.h`）与 `hv_restore()`
  （`arch/arm64/include/arch/vm.h`）都是 `__attribute__((noreturn))`，编译器可证明落到
  `vm_is_paused(cv->owner)` 时 `cv != NULL`。而且 `cv == NULL` 只能在 idle 窗口发生，
  那条路已经 `return`。与既有 `vgic_sgi_drain_vcpu(cv)` 的假设同一条。
- **一个既有路径被延长了寿命，但不是本次引入**：暂停后 pCPU 可能 idle，而 `cur_vcpu` 仍是
  那个"RUNNABLE 但不在队列里"的 vCPU；此时若有人给这个 vCPU 注入 vSPI（如 `vm_console`），
  kick 会落到 idle 窗口 → 排空/`vgic_reload_spi_lr()` 作用在未加载的 vCPU 上。同样的形状在
  HV-004 之前就对"BLOCKED 的驻留者 + idle pCPU"成立，本次没有改变它（不在本片范围）。

### 4.8 状态位与查询

- 查询字：`vmid[15:0] | service[16] | online[17..] | paused[32]`。`VCPUS_PER_VM == 2` ⇒ `online`
  最多占 bit 18，**bit 32 无重叠**；ADR-0022 规则 6"返回字只能追加高位"满足。
- `HC_VM_QUERY` 的 `online` 用 `vm_online_vcpus()`（`state != VCPU_OFF` 计数），暂停不改状态
  → 暂停 VM 仍报 `online=1`（实测 `HCALL: vm0 peer paused=1 online=1`）。语义自洽：
  暂停是"不被调度"，不是"下线"；这也正是 F6 让 shell 用 `paused` 覆盖 `on` 的理由。
- `hc_vm_pause/hc_vm_resume` 不写 `*ret`，`vmexit.c` 无条件 `regs->x[1] = ret`（`ret` 初值 0）
  → guest 读到 `x1 = 0`，**不会泄露上一次调用的残留值**。

### 4.9 权限

- 走**表项** `roles = HC_ROLE_SERVICE`，`hypercall_dispatch()` 在调用处理函数**之前**
  `(roles & caller_roles) == 0 → HC_EPERM`（实验 D 已证承重）。
- 处理函数内**没有** service 判断（读文件确认）；参数校验（宽度、`ENODEV`、自己→`EINVAL`）
  全部在处理函数内，且**在处理函数里才第一次看 `a1`** → guest 无论传什么都得到 `EPERM`，
  **没有扩大 ADR-0022 记录过的"vmid 存在性泄露"**。
- `guest → HC_VM_PAUSE(0)`（自己）也是 `EPERM` 而非 `EINVAL`：先类别后参数，正确。

### 4.10 回归面

- `git diff ac844e4..HEAD -- tests/svm/expect/`：**只有 `hcall.txt` 一个 hunk，7 行纯追加**
  （`+` 行，无删除、无修改）；其余 11 个 expect 文件零改动。
- 12 场景全绿（§2.2），`test-linux` ×2 绿 → 暂停语义没有污染正常调度/抢占/双 VM 时间片。
- 改动的非测试文件：`vm.h`(+36/-0)、`vm.c`(+1)、`sched.c`(+40)、`sched.h`(+8)、
  `hypercall.c`(+72/-5)、`hypercall.h`(+3)、`irq_handler.c`(+15)、`hv_shell.c`(+12/-6)。
  `hv_shell.c` 是唯一被改的既有显示路径：`vm_list` 的 `on`/`halted` 行仍被 `test-qemu-shell`
  的 `"   0    0-1  on      *"` / `"   1    2-3  on"` 钉住并通过。

### 4.11 构建与边界

- 所有构建 `-Werror`，全量日志 `warning:` 0、`error:` 0（arm64 双 VM、x86 stub、host ×4）。
- `check_offsets` / `check_offsets-target` 通过：`struct vm` 尾部新增 `bool paused` 不在
  asm 可见前缀，`struct vcpu`/`struct percpu` 未动。
- `check-arch-boundary` 通过；人工确认新代码无 arch 头：
  `vm.h`（无新增 include）、`sched.h`/`sched.c`（include 列表未改）、
  `hypercall.c` 新增的 `#include <sched.h>` 属 common 层。
- `bool` 原子操作无 libatomic 依赖：`aarch64-linux-gnu-nm -u hypervisor.elf` **零未定义符号**，
  反汇编可见 `ldaxrb`/`swpalb` 一族（8 处），说明 `__atomic_load_n`/`__atomic_exchange_n`
  在 freestanding 下内联为单拷贝原子字节指令，内存序真实生效。

### 4.12 与研发/架构师记录的不一致

| # | 记录 | 事实 |
|---|---|---|
| D1 | 计划 T2/Review Focus #1 + 任务书预期："删掉 kick 分支 → hcall 红（VM1 观察不到冻结空洞）" | **删掉后 hcall 仍全绿**（§3 实验 B）。量子 tick 单独即可换下，hcall 场景分辨不出 kick 有无 |
| D2 | 设计 F2 / ADR-0023 Decision 1 写"`sched_pick` 弹出已暂停 VM 的 vCPU 时直接丢弃"；计划 T1 写 "`sched_pick()`/`sched_loop()` 弹出 paused 的 vCPU 时 `continue`" | 实现只在 `sched_loop` 里丢弃；`sched_pick()` 本身零改动（仍是纯 dequeue）。语义等价（`pick` 是唯一出队点），但按 ADR/计划去 `sched_pick` 里找会找不到 |
| D3 | 计划 T2 的验证标准"`make test` 12 场景仍绿（此刻还没有场景触发暂停，属编译面）" | 成立；但 T3 之后 hcall 已经触发暂停，T2 的标准不再能描述 kick 分支的验证强度（见 D1） |
| D4 | 计划 T4 "架构师写 ADR-0023、devlog、报告、Linear 同步" | ADR-0023 在工作区**未跟踪**（不在任何提交里）；`docs/devlog` 无 HV-004 条目；报告即本文件 |

## 5. 阻塞问题

**无。**

复核过的"候选阻塞"及为何不阻塞：

| 候选 | 结论 |
|---|---|
| kick 分支无 gate 覆盖（D1） | 不阻塞：正确性由量子 tick 兜底（≤10 ms），kick 是延迟优化；ADR-0023 已如实这样写；分支经探针确认被走到。但**必须把"hcall 不证明 kick"写进后续审查者的认知**，不要再把 Review Focus #1 当 kick 的回归门 |
| `ON_PENDING` 只有 host 覆盖 | 不阻塞：host 断言精确钉住，且状态机只有 `PICK` 一条出边（可静态论证"无 resume 就永久丢"），端到端场景在当前 guest 里不可构造 |
| F6（shell `paused` 列）零覆盖 | 不阻塞：纯显示，DoD 未要求；既有 `on`/`halted` 格式有回归门且通过 |
| ADR 说 `sched_pick` 丢弃（D2） | 不阻塞：文档措辞与实现位置不符，语义一致；建议把 ADR/F2 的"`sched_pick`"改成"`sched_loop` 在 pick 之后丢弃" |

## 6. 非阻塞观察（建议后续处理，不阻断合并）

- **N1**（对应 D1）：把"kick 分支"的验证方式写清楚：要么在 ADR/注释里标明"延迟优化，无回归门，
  正确性由量子 tick 兜底"，要么将来在 host 侧给 kick handler 做一个可测的抽象（现在它是 arch
  代码，host 不可达）。
- **N2**（对应 D3/§4.3）：`ON_PENDING` 竞争只有 host 单点。若要端到端覆盖，需要一个"暂停窗口内
  发起 CPU_ON"的场景（暂停 VM1 → VM1 恢复后由 VM0 通知它 CPU_ON？当前 ABI 没有跨 VM 通知，
  所以短期只能保持 host 覆盖）。这条应作为已知缺口记录，而不是"已覆盖"。
- **N3**（对应 D2）：文档措辞修正（ADR-0023 Decision 1、设计 F2、计划 T1/T2 的验证标准）。
- **N4**（对应 D4）：ADR-0023 仍未跟踪；`git log` 里没有它，任何人 `git checkout` 到该分支都
  拿不到这份裁定。Linear 的 H-107 同步我也无法从仓库侧核实。
- **N5**：F6 无测试。`vm_list` 的 `paused` 行、`paused` 与 `on` 的列宽差异只在"`on` 行仍对齐"
  这一半被测到。
- **N6**：`hc_vm_query` 的宽度检查（`a1 >> HC_VMID_BITS != 0U`）与 `vm_arg_target()` 里重复了一次。
  合并会需要参数化"目标是自己时返回什么码"（`QUERY` → `EPERM`，`PAUSE/RESUME` → `EINVAL`），
  反而更绕；保持现状可接受。
- **N7**：guest 场景的时序编排依赖固定绝对偏移与"两 VM entry 对齐"。实测 6/6 稳定、裕量 1.3 s /
  4.5 s；但它是**脆弱契约**：任何新增的 hypervisor `printk` 只要落在 guest 行中间就会打断逐行匹配。
  建议在 `tests/svm/cases/hcall.c` 的时序注释里补一句"不要再加 printk"。

## 7. 已知限制（沿用设计 §6，本轮未消除）

- **异步生效**：`HC_VM_PAUSE` 返回不代表目标已离开硬件；最坏一个量子（≤10 ms）或一次 kick 延迟。
  没有确认协议（触发条件 = 首个要求"停稳后复用资源"的消费者）。
- **只有 `RUNNING ↔ PAUSED`**：无 `CREATED/READY/DESTROYED`、无事件通道（等 VM 创建/销毁切片）。
- **暂停不保存额外状态**：vCPU 上下文留在自己的影子与 `vcpu_arch_put` 路径，与普通换出相同。
- **只 Service VM 能暂停/恢复**，跨 VM 通知没有。
- **本轮新增记录**：暂停期间若有 vSPI 注入到该 VM 的 vCPU，会在"pCPU idle + vCPU 未加载"的
  上下文里写 live LR（§4.7）。这是 HV-004 之前就存在的形状，本片没有扩大，但暂停让它更容易被
  用户操作（对暂停 VM 做 `vm_console`）触发——建议列入 H2 的"暂停期中断投递"待办。

## 8. 环境与 HEAD

```
repo    /home/corsair/Music/virtual/hypervisor
branch  feature/hv004-vm-pause
HEAD    e9c434cf53ff5c075191553acb9df92c618c744d  fix(hv004): resume must re-queue an ON_PENDING vCPU too
        804741348edeef07a58a22620ce423bee28c07e1  test(hv004): gate pause/resume through the hcall scenario
        d2153e37bdec881672e5e51537d2cc0376b9d379  feat(hv004): force a paused VM's resident vCPU off its pCPU
        fcde327                                  feat(hv004): VM pause/resume state and its scheduler semantics
        e3adee7                                  test(hv004): host cases for VM pause/resume (RED)
base    ac844e4 (dev)
diff    git diff --stat ac844e4..HEAD → 14 files, +570/-19
未跟踪  docs/adr/0023-vm-pause-is-a-scheduling-predicate.md（架构师，N4）
        scripts/plugctl.sh（非本轮）
env     PATH=/tmp/wrapbin:$PATH
        LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib
        CROSS_COMPILE=aarch64-linux-gnu-  HV_NR_CPUS=4
tests   QEMU: qemu-system-aarch64 (gic-version=3, 4 pCPU, 4 GB)
        Linux: /home/corsair/Music/virtual/linux-6.12.103/arch/arm64/boot/Image（sha256 预检 OK）
               /tmp/initramfs.cpio.gz（sha256 预检 OK）
还原    4 项破坏实验 + 1 项探针，全部 git checkout；末次 git status 只剩上述两条未跟踪项
```

## 9. 下一步

1. 合并前唯一建议动作：修正 §4.12-D2 的文档措辞（把"`sched_pick` 丢弃"改成"`sched_loop` 在
   pick 之后丢弃"），避免下一位审查者按 ADR 找不到代码。
2. 把 ADR-0023 纳入提交（N4），否则这份裁定随分支丢失。
3. 把 N1/N2 写进 HV-004 的 Linear 评论：**kick 分支无回归门（由量子 tick 兜底）**、
   **`ON_PENDING` 只有 host 覆盖**。这是本片唯一"看起来被覆盖、实际没有"的两个点。
