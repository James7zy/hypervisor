# HV-011/012（第一刀）Stage-2 表池 / 映射 API / fault 闭环 — 测试专家独立复验（2026-11-08）

> 分支：`feature/hv011-s2-tables`，复验顶点 `5885723`（`58857234d69868c446a55b4418e9300578a67744`），
> 基线 `03232ad`。本报告是 HV-011/012 第一刀的**测试专家独立复验**：全部 gate 自己重跑，不采信研发自报；
> 另做了 11 项受控破坏实验（等价自检、MAIR 槽序、GIC/UART 洞、park→halt、池位图），全部 `git checkout` 还原。
> 设计：[HV-011 设计](../superpowers/specs/2026-11-08-hv011-stage2-allocator-design.md)（G1–G7）；
> 计划：[HV-011 实施计划](../superpowers/plans/2026-11-08-hv011-stage2-allocator.md)（T1–T3 + Review Focus）。

## 1. 复验范围与结论

| # | Gate | 结果 | exit | 关键原文 |
|---|---|---|---|---|
| 1 | `make host-test` | 绿，5 套 | 0 | `PASS (195 assertions)` / `scheduler PASS (60 assertions)` / `state race PASS (500000 rounds)` / `hypercall PASS (57 assertions)` / `s2 table PASS (75 assertions)` |
| 2 | `make clean && make test`（从零） | 绿，**13/13** 场景 | 0 | 4 项静态检查 `PASS` + 13 个 `ALL PASS`，`PASS:` 150 行，`FAIL` 0 条，`warning:` **0** 条 |
| 3 | `make test-qemu-s2fault` ×5 | 绿 5/5 | 0×5 | 每次 5 行断言全 `PASS:` + `ALL PASS` |
| 4 | `make test-linux` ×2（加做 2 次留 exit 码，共 4 次） | 绿 4/4 | 0 × 4 | `boot/topology/console: PASS` + `survivor timers: …` + `Linux XVM PASS` |
| 5 | `make test-x86-stub`（`rm -rf build/test-x86` 后全新重建） | 绿 | 0 | 链接行含 `build/test-x86/obj/common/mmu/s2_table.o`；`warning:` 0 条 |
| 5b | `make check-arch-boundary` | 绿 | 0 | `PASS: arch boundary holds` |
| 5c | `make check-offsets` / `check-offsets-target` | 绿 | 0 | `PASS: all struct offsets match assembly macros` / `PASS: cross-compiled struct offsets match assembly macros` |
| 6 | 11 项承重实验后回归（还原后重跑 `make clean && make test` + `host-test`） | 绿 | 0 | 13/13 场景、`s2 table PASS (75 assertions)`、工作树 `git status` 除未跟踪 `scripts/plugctl.sh` 外干净 |

**结论：5 套必需 gate 全绿，13 个 QEMU 场景全绿，`-Werror` 零告警；11 项受控破坏都按预期打红（或按预期不可见，见 P11）对应门禁，
等价自检、fault park、池位图三处都承重。未发现阻塞问题。** 追加 3 条非阻塞缺陷（诊断标号、attr 越界读、
pa 静默截断）与 3 条已知限制补充，见 §6/§7。

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
build/host/s2_test
s2 table PASS (75 assertions)
```

### 2.2 `make clean && make test`（exit 0）

静态检查原文：

```
PASS: arch boundary holds
PASS: all struct offsets match assembly macros
PASS: cross-compiled struct offsets match assembly macros
（test-x86-stub：x86_64 stub 全量重编 + 链接，无输出即成功）
```

13 个 QEMU 场景（`FAIL` 0 条，`warning:` 0 条，`PASS:` 150 行）：

```
basic / vtimer / traps / psci / dual / shell / vspi / ctxsw / preempt / xvm / xvm2 / hcall / s2fault
ALL PASS × 12 + VSPI ALL PASS × 1
```

新场景尾部原文：

```
PASS: 'Hello from EL2'
PASS: 'SVM: s2fault vm0 reading unmapped IPA'
PASS: '[hv] s2 fault: vCPU0 ipa=0x80000000 fsc=0x5 ec=0x24'
PASS: 'SVM: hello from VM1'
PASS: 'SVM HVC: done (x1=0x10001)'
ALL PASS
```

### 2.3 `make test-qemu-s2fault` ×5（exit 0,0,0,0,0）

五次输出逐字节同形：`PASS:` 5 行 + `ALL PASS`。

### 2.4 `make test-linux` ×4（exit 0 ×4；前 2 次为任务要求，后 2 次重跑并显式留 exit 码）

```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (356, 339) -> (795, 748); console: PASS
Linux XVM PASS
```
```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (339, 295) -> (773, 773); console: PASS
Linux XVM PASS
```
```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (328, 243) -> (755, 693); console: PASS
Linux XVM PASS
```
```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (340, 336) -> (771, 803); console: PASS
Linux XVM PASS
```

真实 Linux 双 VM 到 shell 门禁 4/4 绿 → Stage-2 重建没有改变 guest 内存视图（等价性的外部证明）。

### 2.5 `make test-x86-stub`（`rm -rf build/test-x86` 后，exit 0）

```
x86_64-linux-gnu-ld -T hypervisor/arch/x86/board/stub/linker.lds --no-warn-rwx-segments -o build/test-x86/hypervisor.elf \
  … build/test-x86/obj/common/mmu/s2_table.o build/test-x86/obj/common/vm/vm.o … build/test-x86/obj/arch/x86/stub/hooks.o …
x86_64-linux-gnu-objcopy -O binary build/test-x86/hypervisor.elf build/test-x86/hypervisor.bin
（warning: 0 条）
```

`vm_arch_init()` 改成 `int` 后 x86 stub 同步返回 0，跨 arch 编译面干净。

## 3. 承重实验（11 项受控破坏，全部已还原）

每一实验都是：改一处 → 重建/QEMU 实跑 → 记录 → `git checkout -- <file>` → `diff` 确认还原。
最终 `git status --short` 只剩未跟踪的 `scripts/plugctl.sh`（非本刀产物）。

| # | 破坏点 | 期望 | 实测 | 还原 |
|---|---|---|---|---|
| P1 | `stage2.c` RAM 块加 `S2_XN`（错一位） | 自检报错、VM 不下发 | `EQUIV FAIL L1[1]=0x400000400007fd want 0x400007fd` → `VM0 table build failed (-1)` → `VM0 arch init failed; VM left OFF`；QEMU 继续跑到超时（exit 124，非崩溃） | ✔ `diff` 一致 |
| P2 | 同上但**只坏 VM0**（`m->id == 0 ? S2_XN : 0`） | VM0 停、**VM1 照跑** | VM0 三条失败行 + `SVM: hello from VM1` + `[hv] SVM HVC: done (x1=0x10001)`（同 pCPU0） | ✔ |
| P3 | 负向对照：失败通道里加 `cpu_arch_halt()`（`vm.c`） | VM1 不再出现 | VM0 三条失败行之后**再无任何输出**（VM1/pCPU1-3 都没上线） | ✔ |
| P4 | `s2_table.c` 的 Normal WB `SH` 0x3→0x2（Inner→Outer） | host + QEMU 双红 | host：5 条 `FAIL tests/host/s2_test.c:…`（exit 2）；QEMU：`EQUIV FAIL L1[1]=0x400006fd want 0x400007fd` | ✔ |
| P5 | 删自检（`if (rc == 0 && 0 && …)`）+ P1 的错位 | 静默通过 | 无 `EQUIV FAIL`，两个 VM 带着 XN 的 RAM 直接进场，随后各打一行 `[hv] unexpected exit cpu0 EC=0x20 … ELR=0x40200000`（EC=0x20 取指权限 fault）→ 自检承重 | ✔ |
| P6 | 去掉 GIC 洞（把 GIC 2 MB 窗口一起 map） | 自检点名 l2[64] | `EQUIV FAIL l2[64]=0x400000080006c5 want 0x0` | ✔ |
| P7 | 去掉 UART 洞 | 自检点名 l2[72] | `EQUIV FAIL l2[72]=0x400000090006c5 want 0x0` | ✔ |
| P8 | 设备 1 GB 一次 map（块形状由 2 MB 块变 1 GB 块） | 自检拒绝 | `EQUIV FAIL L1[0]=0x400000000006c5 is not a table` | ✔ |
| P9 | `vmexit.c` 的 `park_current_vcpu` 换回 `cpu_arch_halt()` | `s2fault` 红 | exit 2：`FAIL: 'SVM: hello from VM1' not found` + `FAIL: 'SVM HVC: done (x1=0x10001)' not found` | ✔ |
| P10 | `s2_pool_free` 删位图清理 | host 红 | 4 条 `FAIL tests/host/s2_test.c:88/89/95/161`（`s2 table FAIL (75 assertions)`，exit 2） | ✔ |
| P11 | 按 spec G3 交换 `MAIR_EL2` 两槽（`el2_mmu.h`） | 见 §4 | **QEMU 上仍能正常启动**（QEMU 不建模 cacheability/内存属性）→ 该偏离在 QEMU 不可见，详见 §4 | ✔ |

P11 的结论不是"实验失败"，而是复验发现的一个重要事实：G3 的方向性错误**不能被 QEMU gate 抓住**，
只能靠规范阅读与评审裁定。因此研发的偏离必须写进 spec/ADR，而不是靠场景兜底。

## 4. 对抗性审查（逐项结论）

### 4.1 等价性（G6 启动自检）

- 自检是**逐字比对**整条描述符（不是只看有效位），所以 RAM 块多一个 XN（P1）、SH 从 ISH 变 OSH（P4）、
  设备块形状变化（P8）都会被点名；错误信息带 `l1[i]/l2[i]` 与期望值，定位成本低。
- 覆盖面经实测确认：设备 512 个 2 MB 块（`REF_DEV_BLOCK`）+ GIC 2 MB 窗口不映射（P6 命中 `l2[64]`）+
  PL011 2 MB 窗口不映射（P7 命中 `l2[72]`）+ RAM 1 GB 块（`REF_RAM_BLOCK(ram_pa)`）+ 其余 510 个 L1 条目为 0。
  GICD/GICR 同窗（`0x08000000>>21 == 0x080A0000>>21`），代码另有一条 `WARN` 兜住"将来跨窗"的板级变化。
- 自检失败**不停 pCPU**：P1/P2 证明失败后 VM 留 OFF、hypervisor 继续跑；P3 的负向对照证明"如果改成 halt，
  同 pCPU 的另一个 VM 就完全起不来"。这一条是本刀最关键的语义，钉住了。
- 与基线逐字节核对（不只看自检）：基线 `03232ad:hypervisor/arch/arm64/mmu/stage2.c` 的设备块
  `(i<<21)|0x1|(0x1<<2)|(0x3<<6)|(0x2<<8)|(1<<10)|(1<<54)`、RAM 块
  `(ram_pa & 0xFFFFC0000000)|0x1|(0xF<<2)|(0x3<<6)|(0x3<<8)|(1<<10)` 与新 `REF_DEV_BLOCK`/`REF_RAM_BLOCK`
  是同一组常量；基线用 `0xFFFFC0000000` 掩码、新代码用 `~0x3FFFFFFF`，对 ≤2^32 的 `ram_pa` 等价。VTCR、
  VTTBR 装置（每个 vCPU 都写）不变。**等价性结论成立**，不是自检自我循环。

### 4.2 映射 API（`s2_map` / `s2_unmap` / `s2_protect`）

仓库内 host 用例（75 断言）之外，我在 `/tmp/s2_extra.c`（**不落库**）针对评审清单补了 12 组独立断言，全部 PASS：

| 检查 | 结论 |
|---|---|
| 块选择：1 GB / 2 MB / 4 KB | 对齐且 `len≥` 粒度才升块；`pa` 不对齐时**退化到 4 KB 页**，不会错误地用块 ✔ |
| 跨块区间 | 4 KB 页跨 2 MB 窗口（两个 L3）✔；跨 1 GB L1 边界（两个 2 MB 块）✔；`1 GB + 4 KB` 尾部接续 ✔ |
| `len` 非 4 KB 对齐 / 0 / 越过 2^39 | `S2_EINVAL`，且不留部分映射（首个粒度就拒）✔（`arg_case`） |
| 重复映射 | `S2_EINVAL`，原描述符 OA **不变**（不静默覆盖）✔ |
| 池耗尽 | `S2_ENOMEM`（`exhaustion_case`）✔；**中途 ENOMEM 时已装好的部分保留**（见 §6-4）✔ 已实测 |
| `s2_unmap` 回收空表页 | 单页 unmap 后 L1 条目归 0、池回满（`map_page_case`）；跨窗 unmap 同样回收 ✔ |
| 部分范围 unmap | 已覆盖的粒度**先被清掉**再返回 `S2_EINVAL`（无回滚），空 L2 照样还给池 ✔ |
| `s2_protect` 只改属性 | OA 与 `[1:0]` 类型位不变，MemAttr/S2AP/XN 按 attr 改写；同属性重放 `flush=false` ✔ |
| `s2_protect` 需要拆块 | `S2_EINVAL`（本片不做拆分）✔ |
| `attr` 越界（selector 2/3） | **未校验**：ASan 报 `global-buffer-overflow` 于 `s2_encode`，且 `s2_map` 返回 0，描述符 MemAttr=0b0000/SH=0b00（见 §6-2） |
| `pa` ≥ 2^48 | **未校验**：`D_OA_MASK` 静默截断（`pa=1<<52` → OA=`0x0`），见 §6-3 |

### 4.3 fault 路径（`park_current_vcpu` / `data_abort_report`）

- 用 `VCPU_EV_OFF`：`vcpu_state_step(VCPU_EV_OFF)` → `vcpu_state_stop` 是**无条件原子 exchange 到 OFF**，
  从 RUNNING/BLOCKED 都能落；`sched_loop` 的 `prev` 回填分支随后看到非 RUNNING，转去 `sched_pick`。
  与 `psci_cpu_off_self()` 同形（`vcpu_state_step` + `dsb ish` + `hv_restore`）。
- **CPU_ON 复活**：状态机允许 —— `CPU_ON` 的前置是目标 `state == VCPU_OFF`（`vpsci.c:38-40`），命中后把寄存器
  重新 author 成 warm reset（`elr_el2=entry`、`spsr_el2=0x3C5`、GPR 归零、`launched=false`）。所以被 park 的
  vCPU 与"CPU_OFF 过的 vCPU"在 guest 看来完全一样，`Linux cpu hotplug` 语义不变。**风险**：恶意/有 bug 的
  guest 可以自己 `CPU_ON` 复活刚 fault 的 vCPU，形成"fault→CPU_ON→fault"的自伤循环；影响面限于该 guest
  自己（不占 pCPU 之外的资源、不影响其他 VM），本轮判定**可接受**，记入 §7。
- `ISV=0` 与 `EC ≠ 0x24`：两条都走 park，不再 halt。`ISV=0` 先打既有 `MMIO: ISV=0 unsupported` 再打
  `[hv] s2 fault`（两次诊断，语义清楚）；`EC=0x20`（取指 abort）由 P5/P9 类实验实测：打
  `[hv] unexpected exit cpu0 EC=0x20 …` 后 park，同 pCPU 另一 VM 照跑（P5 里两个 VM 都坏掉所以只看到诊断行；
  另一组实验里 VM0 单独取指 fault 后 `SVM: hello from VM1` + `SVM HVC: done` 均出现）。
- `hv_restore` 之前的 `dsb ish`：与 `psci_cpu_off_self()` 完全一致，属同一族路径的形状要求；状态发布本身由
  `vcpu_state_step` 的 acq_rel RMW 保证，`dsb` 是"先落状态再 unwind"的显式化。**不需要加也不需要删。**

### 4.4 TLB（`stage2_flush_vm`）

- 全仓 grep：`stage2_flush_vm()` 唯一调用者是 `stage2_init()` 末尾，而此刻任何 vCPU 都还没装载 VTTBR_EL2
  （`stage2_activate` 之后才写寄存器），所以那次 `tlbi vmalls12e1is` 事实上是空操作。
- `s2_map/unmap/protect` 的唯一调用者也是 `stage2_init()`（`s2_pool_alloc` 只在池内部），`*flush` 出参在
  唯一调用点被收集但**从未被检查**。
- 判定：**契约闲置，不是遗漏**。当前没有动态映射消费者，因此不存在"guest 读到旧描述符"的窗口；风险窗口在
  第一个动态 map 消费者落地时开启（DM 分内存 / MMIO 直通）。届时正确形状是 `if (flush) stage2_flush_vm();`，
  建议在 ADR-0025 里把这条写成硬约束（API 无法强制调用者刷 TLB）。

### 4.5 承重（见 §3）

`park→halt`（P9）、删自检（P5）、删池位图清理（P10）三项全部按预期打红 ⇒ 这三处测试不是摆设。

### 4.6 回归面与构建

- 既有 12 个场景的 `tests/svm/expect/*.txt` **零改动**（`git diff 03232ad..HEAD -- tests/` 只有新增
  `s2fault.c`/`s2fault.txt`/`s2_test.c`）；13/13 绿。
- `-Werror` 零告警：arm64 全量（`make clean && make test`）与 x86 stub（`rm -rf build/test-x86` 重建）各 0 条
  `warning:`。
- host 层不带 arch 头：`hypervisor/common/mmu/s2_table.c` 只 `#include <s2_table.h>`，`check-arch-boundary`
  通过；`make host-test` 直接用它编译。
- `check-offsets` / `check-offsets-target` 通过 ⇒ 没有动 `struct vcpu`/`struct percpu` 的 asm 可见前缀。

## 5. 阻塞问题

**无。** 五套必需 gate 全绿，13/13 场景绿，等价自检、fault park、池位图三处经受控破坏证明承重。

## 6. 非阻塞观察（建议本刀内顺手修，或明确记入已知限制）

1. **`data_abort_report()` 的 vCPU 标号印的是 pCPU 号。** `current_vcpu_id()` 返回
   `cpu_arch_this_percpu()->cpu_id`（pCPU），格式串却写 `vCPU%u`，与仓库既有 `[hv] unexpected exit cpu%u`
   的约定（`cpu`=pCPU、`vCPU`=vCPU）自相矛盾。实测复现：用 `svm_dual` profile 把 `svm-s2fault.bin` 装到 VM1
   （vCPU0 亲和 pCPU2），日志为 `[hv] s2 fault: vCPU2 ipa=0x80000000 fsc=0x5 ec=0x24` —— faulting vCPU 是
   VM1 vCPU0，却报成 `vCPU2`。`s2fault` 场景因为 vCPU0 恰好在 pCPU0 而掩盖了它。
   建议一行改成 `current_vcpu()->vcpu_idx`（expect 行 `vCPU0` 不变），或把格式串改成 `pcpu%u/vcpu%u` 双字段。
2. **`attr` selector 未校验 → 数组越界读。** `s2_encode` 用 `attr & S2_ATTR_MASK`（2 bit，可到 3）索引只有两项的
   `s2_attr_bits[]`；ASan 在 `s2_encode` 报 `global-buffer-overflow`，`s2_map` 仍返回 0，描述符静默变成
   MemAttr=0b0000 / SH=0b00（Device-nGnRnE 不可共享）。当前两个调用点都传合法常量 ⇒ 无现网影响。
   建议 `s2_map/s2_protect` 入口加 `if ((attr & S2_ATTR_MASK) > S2_ATTR_DEVICE_nGnRE) return S2_EINVAL;`。
3. **`pa` 上界未校验。** `pa ≥ 2^48` 被 `D_OA_MASK` 静默截断（实测 `pa=1<<52` → OA=`0x0`）。`ram_pa` 是板级常量，
   当前无消费者；建议加一条 `pa` 范围检查，或写进 §7。
4. **`s2_map` 无回滚（研发已声明，复验确认并细化）。** 中途 `S2_ENOMEM` 时：已安装的粒度**保持映射**，
   已分配的表页**留在池里**（实测 pool=2、map 4 MB 的 4 KB 页 → 第一个 2 MB 窗口 512 页装好后返回 `ENOMEM`，
   池空闲数为 0）。当前唯一调用者是 `stage2_init()`（失败即整 VM 不下发），所以无现网影响；但动态消费者必须
   自己 unmap 已装部分，否则从池里永久漏掉表页。建议写进 spec §6 已知限制（目前只写了"拆块保守拒绝"），本报告 §7 已登记。
5. **`*flush` 契约未接上消费点。** 见 §4.4：`stage2_init` 收了 `flush` 却不看，靠末尾无条件刷一次兜底。
   现在无害；第一个动态调用者落地前应把"看 `flush` 再刷"写死。
6. **`s2_protect` 的 `p` 参数未用（`(void)p`）。** 为签名对称保留，可接受，仅记一笔。
7. **`common/mmu/s2_table.c` 承载 ARM Stage-2 编码。** arch-boundary gate 通过（无 `stage2` 标识符、无 arch 头），
   且 spec G2/Task 1 就是"generic 目录、arch-neutral、纯内存操作"。仅提示：x86 SVM 将来需要同形状的池 + 不同
   编码，届时池可复用、编码要分 arch（本片不必动）。
8. **`s2fault` 复用 `svm_xvm2` profile。** 它只借用拓扑常量（两 VM 的 vCPU0 同在 pCPU0），但因此与 xvm2 的
   survivor 语义耦合：xvm2 若回归，`s2fault` 会连带受影响。case 顶部注释已写明理由，可接受。
   （附带发现：`Makefile` 中 xvm2 target 上方"NOT yet in the `test` aggregate: it is RED on dev HEAD"的注释在
   **基线就已过时**（基线 `test` 聚合已含 `test-qemu-xvm2`），非本刀引入。）
9. **`s2fault` expect 首行少了 `[hv] ` 前缀**（其他 expect 均为 `[hv] Hello from EL2`）。harness 是子串匹配，
   所以照样通过；风格不一致，改不改都行。

## 7. 已知限制（含本轮新增）

沿用 spec §6：保守全刷 TLB（无 range 失效）、guest RAM 仍静态 1 GB 块、无巨页拆分、fault 只 park 不上报 DM、
无按需调页、权限/只读只有 host 层描述符断言（无真实写保护场景）。**本轮新增/细化：**

- **fault park 的 vCPU 可被 guest 自己 `CPU_ON` 复活**（§4.3），fault→CPU_ON→fault 的 guest 自伤循环不受限。
- **`s2_map` 失败不回滚**（§6-4）：部分映射留存 + 表页留在池里。
- **`attr`/`pa` 参数无范围校验**（§6-2/3）。
- **等价自检只覆盖当前 qemu_virt 板级形状**：GICD/GICR 跨 2 MB 窗口时只有 `WARN`，自检会随之为新板改期望值；
  RK3588 上板前必须重算 `REF_*`，否则自检要么假红要么漏检。

## 8. 环境与 HEAD

- 分支 `feature/hv011-s2-tables`，HEAD `58857234d69868c446a55b4418e9300578a67744`（`5885723`），基线 `03232ad`。
- 交叉工具链 `aarch64-linux-gnu-gcc 13.3.0`（`PATH=/tmp/wrapbin:$PATH`、
  `LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib`、`CROSS_COMPILE=aarch64-linux-gnu-`）。
- 宿主 `cc 13.3.0`，QEMU `9.2.4`。
- Linux 门禁镜像：`~/Music/virtual/linux-6.12.103/arch/arm64/boot/Image`（sha256
  `8b518aa6…`）+ `/tmp/initramfs.cpio.gz`（sha256 `f4b7a485…`），均与 `tests/linux-gate.sha256` 一致。
- 未跟踪文件：`scripts/plugctl.sh`（非本刀产物，未改动）。本轮**没有 commit、没有 push、没有新建分支**；
  11 项破坏实验全部 `git checkout` 还原，最终 `git status` 干净。

## 9. G3（MAIR 槽序）的独立判断

**判断：代码对，spec G3 该改。**

1. **规范**：ARM DDI0487（VMSAv8-64）Stage-2 块/页描述符自带 `MemAttr[3:0]`（bits[5:2]）、`S2AP[1:0]`（bits[7:6]）、
   `SH[1:0]`（bits[9:8]）、`AF`（bit[10]），**没有** `AttrIndx` 字段；`MAIR_EL2` 只服务 EL2&0 的 Stage-1 翻译。
   所以"Stage-2 属性靠显式写 `MAIR_EL2` 两个槽位"这个前提本身不成立。
2. **本仓既有事实（强证据）**：基线 `03232ad` 的 Stage-2 表**一直**内联编码 MemAttr
   （`S2_MEMATTR_DEV = 0x1<<2`、`S2_MEMATTR_NORM = 0xF<<2`）、从不写 `MAIR_EL2`，而它能启动 Linux 双 VM 并
   通过 12 场景。说明 Stage-2 的 Normal/Device 属性来自描述符，不来自 MAIR。
3. **MAIR 槽序是 EL2 自己的承重事实**：`el2_mmu.c` 的 `l1_table[0]`（GIC/UART，Device）用 `S1_ATTRIDX(0)`、
   `l1_table[1]`（hypervisor 镜像，Normal）用 `S1_ATTRIDX(1)`，`el2_mmu.h` 明确记录"index 0 = Device-nGnRE，
   index 1 = Normal WB，**改槽序会静默改变 EL2 自己身份映射的属性**"。按 spec G3 的槽序（Normal=0、Device=1）
   写 `MAIR_EL2`，会把这两块描述符的语义整体互换。
4. **实测补充（重要）**：我把 `el2_mmu.h` 的 `MAIR_EL2_VALUE` 按 spec G3 的槽序交换后重建实跑 ——
   **QEMU 上一切照常**（连 Linux/SVM 场景都不会红），因为 QEMU 不建模内存类型/cacheability。也就是说，
   这个错误**不会被任何 QEMU gate 抓住**；在 RK3588 真机上它会变成"GIC/PL011 被当 Normal-WB 缓存访问"或
   "hypervisor 代码段被当 Device 访问"这类难查的现场故障。因此这条偏离必须落到 spec/ADR 文本，靠评审而不是靠门禁。
5. **建议的 spec 修改**：G3 改为"Stage-2 属性由描述符内联的 `MemAttr`/`SH` 编码（Normal WB/WB-RA/Inner-Shareable
   与 Device-nGnRE/Outer-Shareable 两组）；`MAIR_EL2` 不参与 Stage-2，其槽序只属于 EL2 自己的 Stage-1
   （沿用 `el2_mmu.h` 的 slot0=Device / slot1=Normal）；`stage2_init` 里的 `msr mair_el2` 只是把同一常量再写一次、
   不改变任何语义，可以保留为显式化，也可以删。"

## 10. 与研发记录的核对（一致 / 不一致）

| 研发说法 | 复验结论 |
|---|---|
| G3 的 MAIR 槽序与 spec 冲突，以代码为准 | **一致**，且 §9 给出独立依据；QEMU 无法证伪该风险（新增事实） |
| `vm_arch_init()` 改 `int`、失败置 VM OFF、不停 pCPU | **一致**；P2/P3 双向验证 |
| `s2_map` 无回滚 | **一致**，并细化为"部分映射留存 + 表页泄漏"（§6-4 有实测） |
| `s2_protect` 保留未用 pool 参数 | **一致**，无害 |
| `s2fault` 复用 `svm_xvm2` profile | **一致**，附耦合提示（§6-8） |
| `stage2_flush_vm()` 无运行期调用者 | **一致**，判定为契约闲置（§4.4） |
| （未提及）fault 日志的 vCPU 标号 | **新发现**：印的是 pCPU（§6-1），已在 `svm_dual` 上复现 |
| （未提及）`attr`/`pa` 无范围校验 | **新发现**（§6-2/3） |
| `make test` 13 场景、既有 12 场景 expect 零改动 | **一致**（13/13 绿，expect 无改动） |
| `-Werror` 零告警 | **一致**（arm64 全量 + x86 stub 各 0 条） |

## 11. 下一步

1. 把 §9 的 G3 裁定写进 spec（以及 ADR-0025），把 §4.4 的 TLB 契约与 §6-4 的"无回滚"写进已知限制。
2. §6-1（vCPU 标号）、§6-2（attr 校验）、§6-3（pa 校验）各一到两行，建议本刀内补掉，并给 §6-1 补一个
   "vCPU idx ≠ pCPU id"的断言点（现有 `s2fault` 拓扑掩盖了它）。
3. Linear：H-125/H-123/H-127 贴本报告与本轮 gate 证据（提交时附 HEAD `5885723`）。
