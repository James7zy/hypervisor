# HV-005 Hypercall ABI 与陷入分发 — 测试专家独立复验（2026-11-08）

> 分支：`feature/hv005-hypercall-abi`，复验顶点 `9e7f526`（`9e7f526366c11bee69036920a42513c8e0ee3723`），
> 基线 `57bf8df`。本报告是 HV-005 的**测试专家独立复验**：全部 gate 自己重跑，不采信研发自报；
> 另做了 3 项受控破坏实验（分别打在权限判断、ABI 号值、arch 号段判断上）检验测试是否承重。
> 设计：[HV-005 设计](../superpowers/specs/2026-11-08-hv005-hypercall-abi-design.md)；
> 计划：[HV-005 实施计划](../superpowers/plans/2026-11-08-hv005-hypercall-abi.md)；
> 决策：[ADR-0022](../adr/0022-hypercall-abi-vendor-range-table-dispatch.md)（本轮期间他人写入，未跟踪，我未改动）。

## 1. 复验范围与结论

| # | Gate | 结果 | exit | 关键原文 |
|---|---|---|---|---|
| 1 | `make host-test` | 绿 | 0 | `PASS (195 assertions)` / `scheduler PASS (34 assertions)` / `state race PASS (500000 rounds)` / `hypercall PASS (35 assertions)` |
| 2 | `make clean && make test`（从零） | 绿，12/12 场景 | 0 | 4 项静态检查 `PASS` + 场景 `ALL PASS`×11 + `VSPI ALL PASS`×1，`FAIL` 行 0 条，`PASS:` 138 行 |
| 3 | `make test-qemu-hcall` ×5 | 绿 5/5 | 0×5 | 每次 10 行断言全 `PASS:` + `ALL PASS` |
| 4 | `make test-linux` ×2 | 绿 2/2 | 0×2 | `boot/topology/console: PASS` + `Linux XVM PASS` |
| 5 | `make test-x86-stub`（`rm -rf build/test-x86` 后全新重建） | 绿 | 0 | 链接行含 `build/test-x86/obj/common/hypercall/hypercall.o`；`warning:`/`error:` 0 条 |
| 5b | `make check-arch-boundary`（独立再跑） | 绿 | 0 | `PASS: arch boundary holds` |
| 5c | `make check-offsets` / `check-offsets-target` | 绿 | 0 | `PASS: all struct offsets match assembly macros` / `PASS: cross-compiled struct offsets match assembly macros` |
| 6 | 3 项承重实验全部还原后复跑（`make clean && make test` + `host-test` + `test-qemu-hcall`） | 绿 | 0 | 12/12 场景、`hypercall PASS (35 assertions)`、`ALL PASS` |

**结论：五套必需 gate 全绿，12 个 QEMU 场景全绿；3 项受控破坏都按预期把对应 gate 打红，
hcall 场景既承重于权限判断，也承重于 arch 号段接线。未发现阻塞问题。**

研发自报与事实核对：`make test` 场景数 12 ✔；两 VM 打印错峰确为 100 ms / 800 ms
（`tests/svm/cases/hcall.c` 的 `wait_ms(is_service ? 100UL : 800UL)`）✔；`HC_GUEST_DONE`
仍是 `0x87000001` ✔；没有发现研发证据与事实不一致之处。

## 2. Gate 证据（原文摘录）

### 2.1 `make host-test`（exit 0）

```
build/host/host_test
PASS (195 assertions)
build/host/sched_test
scheduler PASS (34 assertions)
build/host/vcpu_state_race_test
state race PASS (500000 rounds)
build/host/hypercall_test
hypercall PASS (35 assertions)
```

### 2.2 `make clean && make test`（exit 0）

静态检查四行原文：

```
PASS: arch boundary holds
PASS: all struct offsets match assembly macros
PASS: cross-compiled struct offsets match assembly macros
（test-x86-stub：x86_64 stub 全量重编 + 链接，无输出即成功）
```

12 个 QEMU 场景（`FAIL` 行 0，`PASS:` 138 行）：

```
basic / vtimer / traps / psci / dual / shell / vspi / ctxsw / preempt / xvm / xvm2 / hcall
ALL PASS × 11 + VSPI ALL PASS × 1
```

新场景尾部原文：

```
PASS: 'HCALL: vm0 version 0.1'
PASS: 'HCALL: vm0 self vmid=0 service=1 online=1'
PASS: 'HCALL: vm0 query peer rc=0'
PASS: 'HCALL: vm0 unused rc=-3'
PASS: 'HCALL: vm1 version 0.1'
PASS: 'HCALL: vm1 self vmid=1 service=0 online=1'
PASS: 'HCALL: vm1 query peer rc=-2'
PASS: 'HCALL: vm1 unused rc=-3'
PASS: 'SVM HVC: done (x1=0x0)'
PASS: 'SVM HVC: done (x1=0x100)'
ALL PASS
```

### 2.3 `make test-qemu-hcall` ×5（exit 0,0,0,0,0）

五次均 `PASS:` 10 行 + `ALL PASS`。按 `scripts/run-qemu.sh` 直接跑原始输出（非 expect 过滤），
两 VM 的行序干净、无交错：

```
HCALL: vm0 version 0.1
HCALL: vm0 self vmid=0 service=1 online=1
HCALL: vm0 query peer rc=0
HCALL: vm0 unused rc=-3
[hv] SVM HVC: done (x1=0x0)
HCALL: vm1 version 0.1
HCALL: vm1 self vmid=1 service=0 online=1
HCALL: vm1 query peer rc=-2
HCALL: vm1 unused rc=-3
[hv] SVM HVC: done (x1=0x100)
```

### 2.4 `make test-linux` ×2（exit 0,0）

```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (347, 349) -> (716, 737); console: PASS
Linux XVM PASS
```

```
parser self-test PASS
boot/topology/console: PASS
survivor timers: (327, 222) -> (773, 655); console: PASS
Linux XVM PASS
```

### 2.5 `make test-x86-stub`（`rm -rf build/test-x86` 后，exit 0）

```
x86_64-linux-gnu-ld -T hypervisor/arch/x86/board/stub/linker.ld --no-warn-rwx-segments -o build/test-x86/hypervisor.elf \
  ... build/test-x86/obj/common/hypercall/hypercall.o build/test-x86/obj/common/vm/vm.o ...
x86_64-linux-gnu-objcopy -O binary build/test-x86/hypervisor.elf build/test-x86/hypervisor.bin
```

`warning:` / `error:` 计数 0。

## 3. 承重实验（受控破坏，全部已还原）

三处破坏都只改一个判断/一个常量，跑完立即 `git checkout`，并用 blob 哈希确认还原：
`vmexit.c` → `2c831438abe1c2a49e5ec744663409a681b1e588`，
`hypercall.c` → `cfd2949…`，`hypercall.h` → `e6e1934…`（均等于 `HEAD:path` 的 blob）。

### 实验 A：删掉 `hc_vm_query` 的 EPERM 判断

改法：`if (target != caller && !vm_is_service(caller)) return HC_EPERM;` → `(void)caller;`。

| 受影响 gate | 结果 | exit | 原文 |
|---|---|---|---|
| `make host-test` | **红** | 2 | `FAIL tests/host/hypercall_test.c:87: hypercall_dispatch(guest, HC_VM_QUERY, 0, 0, &ret) == HC_EPERM`、`FAIL ...:88: ret == 0` |
| `make test-qemu-hcall` | **红** | 2 | 9 行 `FAIL:`，含全部 4 行 vm1 与 `SVM HVC: done (x1=0x100)` |

现场（QEMU 原始输出摘录）：VM1 一旦被判成 Service，它也打印 `HCALL: vm0 …`，
两个 VM 同时按 100 ms 起打印、**逐字节交错**，连 vm0 的行也不再是干净子串；
两次 done 载荷都是 `(x1=0x0)`：

```
HHCCAALLL:L : vvmm00 ve rverssiionon  00..11
[hv] SVM HVC: done (x1=0x0)
[hv] SVM HVC: done (x1=0x0)
```

### 实验 B：`HC_GUEST_DONE` 的 id 改一位

改法：`HC_ID(HC_MOD_GENERIC, 1)` → `HC_ID(HC_MOD_GENERIC, 2)`（即 `0x87000002`）。

| 受影响 gate（抽样 4 个） | 结果 | exit |
|---|---|---|
| `make test-qemu`（basic） | **红** | 2 |
| `make test-qemu-dual` | **红** | 2 |
| `make test-qemu-psci` | **红** | 2 |
| `make test-qemu-hcall` | **红** | 2 |

原文：`FAIL: 'SVM HVC: done (x1=0x10001)' not found in output`（basic/dual）、
`FAIL: 'SVM HVC: done (x1=0x0)' not found in output`（psci/hcall）+ QEMU 超时终止，因为 guest 的
`HC_GUEST_DONE` 不再被识别、`hv_restore` 不再发生。10 个 SVM expect 文件**全部**含
`SVM HVC: done` 行，所以这一改动会让所有 SVM 场景（10/10）集体红，而不是个别红。

### 实验 C（加做）：arch 号段判断改一位

改法：`(func_id & 0xFF000000U) == HVC_VENDOR_BASE` → `== 0x88000000U`。

| 受影响 gate | 结果 | exit | 原文 |
|---|---|---|---|
| `make test-qemu-hcall` | **红** | 2 | `HCALL: FAIL no self`、`HCALL: FAIL version rc`，并出现 `[hv] HVC: unknown func_id=0x87000000` |

说明 arch 侧"把 vendor 号转进 generic 表"这一行本身也被门禁钉住，不只是 generic 表有测试。

### 还原后复跑

```
make host-test            exit 0   hypercall PASS (35 assertions)
make test-qemu-hcall      exit 0   ALL PASS
make clean && make test   exit 0   12/12 场景 ALL PASS（FAIL 行 0）
```

## 4. 对抗性审查（逐项结论）

### 4.1 身份伪造

1. **身份来源**：`handle_hvc()` 只传 `current_vcpu()->owner`
   （`arch/arm64/vmexit/vmexit.c:63-64`）；grep 全树 `hypercall_dispatch(` 在 `hypervisor/` 下
   只有这一个调用点，
   `x1..x5` 没有任何一处参与身份判断。E2 成立。
2. **guest 传任意 `x1` 冒充 Service**：`hc_vm_query` 的越权判断只比 `target != caller`，
   `a1` 仅作为"要查哪个 vmid"。host 用例断言"非 service 调者 + 他人 vmid → `HC_EPERM`
   且 `ret` 不被写"；我用独立探针（`/tmp/hv-probe/`，不在仓库内）复核：guest 查 0 → `-2`，
   `ret` 保持 0。
3. **`vm_by_id` 越界**（独立探针，非仓库代码）：

   | `a1` | 结果 |
   |---|---|
   | `0xFFFF` | `-4` (HC_ENODEV) |
   | `0x10000` | `-4` |
   | `0xFFFFFFFF` | `-4` |
   | `0xFFFFFFFFFFFFFFFF` | `-4` |
   | `0x100000000` / `0x100000001` / `0x100000002` | 全 `-4` |
   | `0xFFFF00000001` | `-4` |

   注意 `0x100000001` 是**截断别名**用例：若没有 `if (a1 > 0xFFFF) return HC_ENODEV;` 这道
   宽度闸，(u32) 截断后等于 1，会**误命中 VM1** 并返回它的状态字。现有代码挡住了。
   `vm_by_id` 本身是 `id == vmid` 线性扫描（`vm[i].id = i` 由 `vm_init` 赋），越界一律 NULL。
4. **`caller == NULL`**：**会崩**（`vm_is_service()` 直接解引用 `v->id`，无 NULL 检查）。
   探针实测 SIGSEGV（exit 139）。**可达性判定：今天不可达**——唯一调用点是
   `handle_hvc()`，`current_vcpu()->owner` 在 `vm_init()` 里对所有 VM 的所有 vCPU 赋值，
   早于任何 guest 能执行；而 HVC（EC=0x16）只能由运行中的 EL1 guest 发出，此时 `cur_vcpu`
   必为该 guest 的 vCPU。但 `hypercall.h` 没有把"caller 非 NULL"写成前置条件，也没有 assert
   ——见 §6.2。

### 4.2 id 冲突与分支顺序

1. **`HC_GUEST_DONE` 仍是 `0x87000001`**：`HC_ID(0,1) = 0x87000000 + (0<<8) + 1 = 0x87000001`
   （实算）；host 用例断言该字面值；`tests/svm/svm_lib.h` 里的 guest 侧常量也仍是
   `0x87000001UL`。实验 B 证明两处必须同值，否则集体红。
2. **vendor 范围判断是高位掩码，不是前缀/范围假设**：`(func_id & 0xFF000000U) == 0x87000000`
   即"bits[31:24] == 0x87"，覆盖 `0x87000000`–`0x87FFFFFF` 整个 SMCCC 32 位 vendor 段。
   实测判定表：

   | id | 顶部字节 | 进 vendor 分发？ |
   |---|---|---|
   | `0x87000000` / `0x87000001` / `0x87000100` / `0x8700FF00` | 0x87 | 是（表内或 `HC_ENOSYS`） |
   | `0x87800000` / `0x87FFFFFF` | 0x87 | 是 → `HC_ENOSYS` |
   | `0x84000000` / `0xC4000000` | 0x84 / 0xC4 | 否 → PSCI 分支 |
   | `0x80000000` / `0x80000001` | 0x80 | 否 → SMCCC base 分支（精确相等，先于 PSCI） |

3. **分支顺序未被破坏**：`handle_hvc()` 顺序为 SMCCC 精确相等 → `svc == 0x84/0xC4` → vendor 掩码
   → 未知（`printk` + `SMCCC_NOT_SUPPORTED`）。结构上 `0x84/0xC4` 不可能满足 vendor 掩码，
   `0x87xxxxxx` 也不会被 `svc` 判断吃掉，两向都不冲突。实测：`psci` 场景仍绿（PSCI 调用仍被
   应答）、`make test-linux` ×2 绿（Linux 走 PSCI `CPU_ON` + SMCCC 探测）、`hcall` 绿。
4. **旁注（不是缺陷）**：SMCCC 64 位调用约定的 vendor 段 `0xC7000000+` **不分发**，落到
   "unknown HVC" → `SMCCC_NOT_SUPPORTED`。E1 明确选了 32 位约定的 `0x87` 段，行为一致；
   若将来有调用方用 64 位约定需新增分支（见 §6.5）。

### 4.3 返回字编码

1. **三段不重叠**：`vmid` 占 bits[15:0]（写前有 `a1 > 0xFFFF` 闸 + `vm[i].id = i` 且
   `NR_VMS` 很小）、`is_service` 占 bit16、在线数占 bits[31:17]。
   `VCPUS_PER_VM = 2`，所以 `count << 17 ≤ 0x40000`，最高到 bit18，不与 bit16 或 vmid 段相交。
   探针实测：VM1 自查询 `0x20001`（vmid=1、svc=0、n=1）；VM2 两个 vCPU 全在线 `0x40002`
   （vmid=2、svc=0、n=2）。host 用例按位断言三段，QEMU expect 用整行文本钉住
   （`service=1 online=1` / `service=0 online=1`）。
2. **`vm_online_vcpus` 与 `hv_shell` 的 `vm_list` 判据是否真的一致**：**一致，且是同一份实现**。
   `dm/hv_shell.c:151` 改为 `bool any_on = vm_any_vcpu_on(m);`，而 `vm_any_vcpu_on()` 就是
   `vm_online_vcpus(v) != 0`（`common/vm/vm.c`）。grep 全树，VM 级(stop)派生态只有这一处实现；
   `vpsci.c`/`vgic.c` 里对 `state != VCPU_OFF` 的读是**单 vCPU** 语义，不是 VM 级派生，不构成漂移。
   实测对照：`make test-qemu-shell` 绿（`vm_list` 两行都是 `on`）；同 profile 的 `hcall` 场景
   两个 VM 都报 `online=1` —— 同一谓词，同一答案。
3. **术语提醒（非缺陷）**：`HC_VM_QUERY` 返回的"vmid"是 `vm->id`（hypervisor 里的 0 起编号，
   也就是 shell `vm_list` 的 ID 列），**不是** `vm_config->vmid`（1 起的 Stage-2 VMID，
   启动行 `SVM: launching VMID=1` 打印的那个）。`hv_shell.c:112` 已有注释说明这个取舍；
   两个"vmid"同名不同义，读代码时容易混。

### 4.4 承重（见 §3）

三处破坏分别打穿"权限判断 → host 用例 + SVM 场景"、"号值兼容 → 10 个 SVM 场景"、
"arch 号段接线 → SVM 场景"，且都在还原后复绿。**没有一处是靠"输出恰好还过得去"通过的。**

### 4.5 回归面

`git diff 57bf8df..HEAD -- tests/svm/expect/` = 1 个文件、10 行新增、0 行删除：
只有新加的 `tests/svm/expect/hcall.txt`。既有 9 个 expect 文件零改动（`basic ctxsw dual
preempt psci traps vtimer xvm xvm2`），既有场景（含 `shell`/`vspi` 两个自有脚本的）全绿。

### 4.6 构建

- `-Werror` 零告警：`make clean && make test` 全量重编日志里 `warning:` 0 条；x86 stub
  全新重建同样 0 条。
- `check_offsets` / `check-offsets-target`：两条 `PASS`（`vm.h` 的改动只是三个函数声明，
  没有动 `struct vm`/`struct vcpu` 布局，与 asm 宏仍然一致）。
- **`hypercall.c` 不含 arch 头**：直接读文件确认 `#include <types.h> <vm.h> <hypercall.h>`
  三条，没有 `<arch/...>`；`include/hypercall.h` 只含 `<types.h>`。`check-arch-boundary.sh`
  除了 grep 还会带 `CC`/`CPPFLAGS` 做**传递预处理**检查，也通过；x86 stub 链接进了
  `common/hypercall/hypercall.o`，说明 generic 侧确实无 arch 依赖。

### 4.7 `hcall` 场景自身是否"假通过"

问题：guest 用 `HC_VM_QUERY(self)` 推 vmid，前提是"答案集唯一"（Service 看全体、普通 guest
只看自己）。VM1 有没有可能也推成 0？

1. **机制**：guest 依次探测 `0..PROBE_VMS-1`（4），遇 `HC_ENODEV` 停、遇 `HC_EPERM` 记
   "被拒"，成功的那个 vmid 记为 self。VM1 探测 0 → 走 `target != caller && !service` →
   `HC_EPERM`；探测 1 → 成功（self=1）；探测 2 → `vm_by_id(2)==NULL` → `HC_ENODEV` 停。
   `seen_denied=1` → `is_service=0`。
2. **VM1 推成 0 的唯一途径**就是 4.1 的权限 bug（能查 VM0）。我把该判断删掉做了实验 A：
   VM1 果然自认 Service、vmid=0，于是它打印 `HCALL: vm0 …`，expect 里的 4 行 `vm1` 与
   `(x1=0x100)` **一行都不会出现** → 红。**所以不是假通过：错误身份一定以"缺 vm1 行 +
   done 载荷不对"的形式暴露。**
3. **额外保险**：done 载荷是 `0x100 * self + fails`（VM0 → `0x0`、VM1 → `0x100`），
   所以"谁被当成谁"写进了 EL2 打印；expect 同时要求两个不同载荷。
4. **反向风险（也是 fail-closed）**：若将来把"不存在的 vmid"也回 `HC_EPERM`，VM0 探测 2 会
   记一次"被拒" → `is_service=0`，此时 `self service bit` 断言（期望 1，实得 0）会红。
   探测循环对意外返回码也有 `expect(rc == 0L, "probe rc")`。
5. **弱断言已被文本补齐**：`expect((x1 >> 17) >= 1UL, "self online")` 与
   `expect((x1 >> 16) == 0UL, "version major")` 单看偏松，但 expect 文件的整行
   `version 0.1` / `online=1` 精确钉住了数值。子串匹配不会被"更长的行"误命中
   （`online=1` 后面只可能跟 `\n`，因为 `VCPUS_PER_VM = 2`）。

### 4.8 研发自报核对

| 自报 | 复核 |
|---|---|
| `make test` 12 场景 | ✔ 12/12（`ALL PASS`×11 + `VSPI ALL PASS`×1） |
| 两 VM 打印错峰 100 ms / 800 ms | ✔ `hcall.c` 的 `wait_ms(is_service ? 100UL : 800UL)` |
| `HC_GUEST_DONE` 值不变 | ✔ `0x87000001`（两处常量） |
| RED→GREEN 提交顺序 | ✔ `115e050` 只有 `Makefile` + host 测试；在 `115e050` 的临时 worktree 里 `make host-test` 确实编译失败（`fatal error: hypercall.h: No such file or directory`） |
| 既有 11 场景零改动 | ✔ expect 目录只有新增 `hcall.txt` |

（worktree `/tmp/hv-red` 用后已 `git worktree remove`，无残留。）

## 5. 阻塞问题

**无。** 五套必需 gate 全绿（12/12 场景）；3 项受控破坏分别把 host 用例、SVM 场景、arch 接线
打红，还原后复绿；身份来自 vCPU 而非参数、`HC_GUEST_DONE` 号值、`vm_by_id` 越界、返回字三段、
shell 与 hypercall 派生状态同源，逐项都能在代码与实测之间对上。

## 6. 非阻塞观察

1. **表项 `roles` 掩码分支目前是死代码**：gcc `--coverage` 跑 host 用例，`hypercall.c`
   30 行覆盖率 96.67%，唯一未执行行是 `hypercall.c:89` 的 `return HC_EPERM`（表项角色检查）——
   本片两个表项都是 `HC_ROLE_ANY`，`caller_roles` 恒含 `HC_ROLE_ANY`，该分支不可能命中。
   这是 E3 要求的形状（等 HV-004 第一个 service-only 调用才活），不算缺陷，但要知道
   "表项权限模型"今天是零覆盖。
2. **`caller == NULL` 未防御也未写进契约**：探针实测 SIGSEGV。今天不可达（见 §4.1.4），
   但 `hypercall.h` 只写了"arch 传 `current_vcpu()->owner`"，没有"非 NULL"前置条件或 assert。
   代价一行；建议在 HV-004 接线（多入口/事件）时补上或明确写成前置条件。
3. **`HC_VM_QUERY` 用 `EPERM` vs `ENODEV` 向普通 guest 泄露"存在哪些 vmid"**（即可推知
   `NR_VMS`）。这正是 `hcall` 场景做自识别的机制，属有意设计；泄露面只有 VM 数量，不含任何
   状态。若要收紧，需同时改场景的发现协议。
4. **`HC_GUEST_DONE` 没有权限门**：任意 VM 可调，效果是该 vCPU 以 RUNNING 状态 park 在
   `sched.c` 的 "HC_GUEST_DONE park" 路径。既有行为、E1 明确保持，安全上是"自伤"不是越权；
   但将来接入真实 guest（HV-004 之后）时它是一个可被 guest 触发的挂起入口，建议放到测试配置
   门后。
5. **只分发 32 位调用约定的 `0x87` 段**：`0xC7000000+`（64 位约定 vendor 段）落到 unknown HVC
   → `SMCCC_NOT_SUPPORTED`。ADR-0022 记了号段选择，但没写"64 位约定不支持"这一句。
6. **`u32 func_id = (u32)regs->x[0]` 截断高位**：`x0 = 0x1_87000000` 会被当成 `HC_VERSION`。
   与 SMCCC 的 u32 id 约定一致，非缺陷；将来若要 64 位 id 得改 arch 入口。
7. **文档交付（spec DoD 4）未闭合**：ADR-0022 存在但**未跟踪、未提交**（本轮开始后才出现，
   时间戳 2026-11-08 03:51，我未改动）；`docs/devlog/2026-11-08-*` 与研发报告尚不存在；
   Linear 同步我无法验证（本会话没有 Linear 工具）。代码 gate 不受影响。
8. **`hcall` 场景的稳定性依赖硬编码错峰**：100 ms / 800 ms 与 10 s 超时。本轮 9 次运行
   （gate2 1 + gate3 5 + 还原复跑 2 + 原始输出 1）全部干净；guest 输出行数增加或 pCPU 起机
   变慢到接近 700 ms 时需要重估。
9. **可读性细节**：`hc_vm_query` 里的 `0xFFFF` 宽度闸是裸魔数，与返回字的位定义分处两处；
   表项 `roles` 与 `HC_OK/EPERM` 常量也不在同一处说明。不影响门禁。

## 7. 已知限制（沿用，未在本轮消除）

- ABI 还没有外部消费者（DM/HSM 未落地）：id 与错误码按"只增不改"承诺，minor 兼容不承诺。
- `HC_VM_QUERY` 只报派生态；VM 级状态（CREATED/PAUSED/DESTROYED）随 HV-004 才落地。
- 没有结构体/大块参数 ABI（`copy_from_gpa` 型）。
- 权限只有"Service VM / 其他"两档（`vm_is_service = id == 0`）。
- x86 侧只到"能编译 + 能链接"：`arch/x86/` 仍无 VMCALL 入口，`test-x86-stub` 不证明 x86 语义。
- 全部验证在 QEMU 9.2.4 / `cortex-a72`（TCG）；RK3588 真机未跑。
- `make test-linux` 本轮 2 次全绿**不构成**"H-174 不会再现"的断言（ADR-0019）。

## 8. 环境与镜像哈希

```
HEAD                9e7f526366c11bee69036920a42513c8e0ee3723  (feature/hv005-hypercall-abi)
基线                57bf8df
QEMU                QEMU emulator version 9.2.4
交叉编译器          aarch64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
x86 交叉编译器      x86_64-linux-gnu-gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
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

两行与 `tests/linux-gate.sha256` 完全一致（`sha256sum` 实测）。

工作树在复验结束时无任何被修改的跟踪文件（`git status --short` 只有三行未跟踪：
`docs/adr/0022-*.md`（他人写入，非本轮产物）、`scripts/plugctl.sh`、本报告）。
三项受控破坏均已 `git checkout` 还原并复跑为绿；临时探针与 RED worktree 都在 `/tmp`，已清理。

## 9. 下一步

1. 关卡②（架构师）可基于本报告 review 合入；阻塞项为零。
2. 合入前补齐 spec DoD 4：把 ADR-0022 加入跟踪并提交，补 devlog 与研发报告，做 Linear 同步
   （H-113 关闭 / H-107 置进行中）。
3. 顺手项（可选，都不进本片门禁）：§6.2 给 `hypercall_dispatch` 补 caller 前置条件或 assert；
   §6.9 把返回字位宽抽成一个符号；ADR-0022 补一句"64 位约定 vendor 段不分发"。
