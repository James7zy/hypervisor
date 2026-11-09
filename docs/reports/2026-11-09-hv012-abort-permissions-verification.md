# HV-012 abort 分类与启动前 RO/XN：独立验收

> 版本说明：保留 `8d538ef` 的六个假阳性及 `74b2222` 的一项阻塞历史；末节记录最终 `05954ec` 独立复验，已解除该阻塞，本切片合入验收 PASS。

## 结论与边界

**当前受控 QEMU profile 的实现行为及必跑门禁通过；未发现阻塞本切片的生产缺陷。** 三类受控变异均得到预期行为 RED，且最后从未变异的真实树 clean 重建再次通过全量 `make test`。这不是依据研发报告作出的结论。

**不是无条件的 checker 健壮性通过**：独立合成日志发现三类额外假阳性（引用标记、重复字段、ESR 与派生字段矛盾），详见下文非阻塞项。真实原始日志已逐条核对，不含这些形态；不得把任意来源文本仅凭 checker=0 当作同等验收证据。父架构师/独立 reviewer 仍须审阅这些限制。

不关闭 H-123；不声称动态 protect/TLBI、LPA2、真机、HSM/DM 或完整 HV-012 验收。本报告不修改实现、不提交、不推送、不换分支。

## 被测版本、范围与环境

- 分支：`feature/hv012-abort-permissions`。
- HEAD：`8d538ef499bd7715e661955b953c35e324d052a1`；基线：`f31a22b`；批准 spec：`ba6eb68`。HEAD 的 spec 与批准版无差异。
- 先读 `CLAUDE.md`、批准 spec、对应 plan，再逐文件审阅 `f31a22b..HEAD` 完整差异及实际调用链。完整差异保存为证据目录 `full.diff`。
- 原始证据根目录：`/tmp/hv012-independent/`。下文日志路径均相对此目录；`README.md`、脚本、`.command`、`.exit`、`.log` 和 `SHA256SUMS` 供独立复查。
- 发布归档：[2026-11-09-hv012-independent.tar.gz](artifacts/2026-11-09-hv012-independent.tar.gz)，
  SHA256 `442d5ac30d4ffb2c405024dcaf68d80ea3c60e80dffdad0148a5adba7521024f`。
  包含各轮文本日志、退出码、语料、变异补丁和复现脚本；排除整仓历史快照、构建产物及
  下载的 Arm 文档副本（来源/版本见上方引用与 spec）。归档中的旧 `SHA256SUMS` 可能引用
  被排除文件，是原执行审计记录；归档整体完整性以上述 SHA256 为准。
  脚本中的 `/tmp` 路径属于当次环境，跨机器复现须先准备工具链、镜像并提取对应提交。
- Linux x86_64 host，GCC 13.3.0（host 与 AArch64 cross）、QEMU 9.2.4、Python 3.12.3；`.config` 为 ARM64 / QEMU_VIRT / DEBUG_UART。
- 环境：`PATH=/tmp/wrapbin:$PATH`，`LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib`，`CROSS_COMPILE=aarch64-linux-gnu-`。完整版本和受保护文件摘要见 `environment.txt`。
- 所有 host 与 QEMU make 目标分命令运行，未触发混合目标的 `need-config` 抑制问题。`gates.sh`、`final.sh` 直接保存被测命令 `$?`，不从管道末端推断。
- 研发 handoff/日志仅用于导航；本报告的 RED/GREEN、门禁、变异和规范下载均独立执行。

## 独立规范复核与对抗审查

独立 curl 下载并保留以下镜像 HTML、纯文本及退出码（均 0）：

- <https://df.lth.se/~getz/ARM/SysReg/AArch64-hpfar_el2.html>
- <https://df.lth.se/~getz/ARM/SysReg/AArch64-far_el2.html>
- <https://df.lth.se/~getz/ARM/SysReg/AArch64-esr_el2.html>

镜像页脚均为 spec 指定的 `26/03/2024 09:49; 67c0ae5282a7629ba0ea0ba7267b43cd4f7939f6`。证据：`spec/*.html`、`spec/*.txt`、`spec/reference-excerpts.txt`。本次没有冒称直接核对最新版 ARM ARM。

| 对抗问题 | 独立审查结果 |
|---|---|
| HPFAR 高位截断是否伪造合法 MMIO？ | `data_abort_mmio.c` 使用 `0xfffffffff0` 解码 HPFAR[39:4]→IPA[47:12]，随后检查 IPA `< 1<<39`，并拒绝 HPFAR[63:40] 非零。不是先截到 39-bit 再使用。镜像 FIPA 子字段 [35:0] 位于父字段 [47:4] 内，对应寄存器 [39:4]。host 覆盖 HPFAR bit31、39、40 越界。 |
| 普通 permission 是否错误信任 HPFAR？ | 镜像只在 S1PTW 等列举条件保证 permission 更新 HPFAR；普通 permission 的 IPA 保守无效，日志输出 `ipa=0x0 ipa_valid=0`。未实施 AT。构造陈旧 HPFAR 的 spy 行为漏洞不等价于已复现 RAM 绕过。 |
| FnV 是否被万能化？ | DFSC/IFSC=0x10 的 synchronous External abort 才按 FnV 表达 FAR 有效性；translation/access-flag/permission 不因保留位置 FnV=1 判 FAR 无效；其他未知类不因 FnV=0 判 FAR 有效。与镜像字段定义一致，host 有正反控制。 |
| S1PTW、CM、ISV、Instruction Abort 是否漏入模拟？ | 仅 EC=0x24、FSC=0x05–07、ISV=1、S1PTW=0、CM=0、IPA 有效准入。0x09–0b、0x0d–0f、未知/扩展 FSC 及 EC=0x20 一律不 dispatch。S1PTW 不拼原始 FAR 偏移冒充完整 IPA。 |
| 快照是否真的只读一次？ | `vmexit_asm.S` lower-EL sync 路径一次保存 ELR、一次读 ESR；`handle_exit()` 一次调用 `abort_snapshot_read()`，其中 FAR/HPFAR 各一次 MRS。模拟和报告共享这个 snapshot，没有失败后重读寄存器。其他 fatal vector/IRQ 的 MRS 不属于本条执行路径。 |
| 拒绝路径还能误 dispatch 或改状态？ | 唯一生产消费者是 `vmexit.c`；准入失败在 dispatch 之前返回。bus 失败时不写 GPR/ELR；成功 read 才写非 x31 目的寄存器，成功访问才推进 ELR 4。Instruction Abort 虽共用 handler 入口，但在共同准入处拒绝。host spy 实测验证。 |
| 诊断值与有效性是否绑定？ | 单条 printk 包含 VM/vCPU/pCPU、EC/FSC/class、FAR/ELR/ESR、IPA/FAR/WnR valid。未知 FAR 可保留原始数值但不能脱离 far_valid 解读；普通 permission 不显示陈旧 HPFAR 为有效 IPA。真实 RO/XN 记录的 ESR 位域与派生字段逐项一致。 |
| fault 是否只停 vCPU？ | Data/Instruction Abort 失败后均 `park_current_vcpu()`：状态 OFF、屏障、`hv_restore()` 回到 pCPU 调度循环；没有生产 halt。停核变异独立验证这一差异可被检测。 |

范围固定为 `run-qemu.sh` 的 cortex-a72，以及 `stage2.c` VTCR 的 4 KiB、T0SZ=25、SL0=1、非 LPA2 profile。保守拒绝额外编码不表示对扩展架构的完整支持。

## 历史同源行为 RED → GREEN

用 `git archive` 分别展开历史提交，未在真实树 checkout。测试链接生产 `data_abort_mmio.c` 与真实 `common/io/mmio.c`，通过注册 spy 观察 dispatch，不以替代分类器作为测试对象。

| 历史快照 | 编译 exit | 行为 exit | 独立结果/证据 |
|---|---:|---:|---|
| `7736e8f` 等行为提取 | 0 | 1 | 把 `ff78ded` 的原始 test 文件移入此 archive，显式 gcc 编译；8 断言、3 失败，permission dispatch/GPR/ELR 断言失败；`history/7736e8f.*`。 |
| `ff78ded` RED | 0 | 1 | 8 断言、3 失败；合法 translation 正向控制通过；`history/ff78ded.{build,behavior}.{log,exit}`。 |
| `a94d4bc` GREEN | 0 | 0 | 581 断言、0 失败；`history/a94d4bc.{build,behavior}.{log,exit}`。 |

独立比较提取前后的 MMIO 主体：ISV-only、SAS/SRT/WnR、zero register、handler 失败和 ELR 更新保持，寄存器读取移至 snapshot；原先错误/过宽的 HPFAR mask 在提取提交中也未被偷偷修复。`history/extraction.diff` 保留比较证据。

首次直接构建 archive 目标因缺 `.config` 返回 2，未生成二进制（尝试执行为 127）；这些原始失败保留为 `*.initial-no-config.*`。随后显式传入 checked-in QEMU defconfig 才得到上表 **编译 0 的行为 RED**，未将配置失败计作红灯。

HEAD/最终重建的 host 结果：abort 581 断言；一般逻辑 195；scheduler 60；state race 500000 轮；hypercall 57；S2 table 108；两个 stage2 builder 分别 21061/20047 断言；原 checker 5 checks、新 checker 30 checks，均无失败。覆盖所有 FSC、access-flag、CM、S1PTW、ISV=0、越界/无注册区域、Instruction Abort；正常尺寸 1/2/4/8、读写、x31、handler 成功/失败以及全寄存器快照不变。L3 protect 还验证 RO/XN 设置与清除、OA/类型/邻页、变更 flush 与 no-op 不 flush。

## Fixture、链接布局与生产隔离

- `svm_s2perm` 复用 `svm_xvm2` 的两 VM 同核拓扑；Makefile 只在该 profile 链接测试目录的 fixture。没有修改公共 vm_config、生产默认 RAM 权限或 builder 授权规则。
- 实际路径：`vm_arch_init → stage2_init → stage2_build/check → fixture → stage2_flush_vm → 保存 VTTBR`；首次 guest load 才 `stage2_activate`。不是运行时 protect。
- fixture 对 VM0 完整 2 MiB `s2_unmap`，逐次 `s2_map(...,4 KiB)` 重建 512 个 L3 叶；再运行原 `stage2_build_check`；随后调用真实 `s2_protect`。VM1 仅核验、不改权限。
- 独立全叶遍历比对 OA、类型、属性、RAM/dev 总覆盖，拒绝额外叶、设备 GIC/UART 陷阱洞被错误映射及非探针权限变化。设备与 VM1 的描述符期望保持原值。fixture 失败返回错误，不绕过 builder。
- 专用 linker ASSERT 分隔代码/数据、RO 页 `0x40210000`、XN 页 `0x40211000`、store 页 `0x40212000`；栈 `[0x403f0000,0x40400000)`，不与探针重叠。
- 最终独立构建 ELF 并 objdump：`0x40212000` 为 `f9000001 str x1,[x0]`；`0x40211000` 为 `d503201f nop`，随后 branch 到入口标记函数；不是动态生成指令。见 `final-disassembly-{0,1}.log`。
- guest volatile read 先验证数据/指令，启动探针检查 SCTLR_EL1.M=0；不能把这里 FAR 与预期 IPA 的对应关系推广给开启 Stage-1 的 Linux。
- 普通 Linux profile 的独立构建 `final-production.exit=0`；nm 中无 `svm_s2perm_fixture`，ELF 中无 fixture 日志字串；专用 profile 则有该符号。首轮 Linux gate ELF 也无 fixture。生产未留运行期权限配置后门。

## 必跑门禁与原始退出码

以下均为直接命令退出码，不把 QEMU 的预期 timeout 当 make/checker 成功码。

| 命令 | exit | 原始日志/退出码 |
|---|---:|---|
| `make host-test` | 0 | `host.log` / `host.exit` |
| `make clean`，随后独立 `make test` | 0 / 0 | `clean.*`、`test.*` |
| `make test-qemu-s2perm` 连续第 1/2/3 轮 | 0 / 0 / 0 | `s2perm-{1,2,3}.{log,exit}`；各轮 raw 在 `round-{1,2,3}/` |
| `make test-linux` | 0 | `linux.log` / `linux.exit`；完整串口 `linux-raw.log` |
| `make test-x86-stub` | 0 | `x86.log` / `x86.exit` |
| `make check-arch-boundary check-offsets check-offsets-target` | 0 | `static.log` / `static.exit`，三个 PASS |
| 最后正常树 `make clean`、`make host-test`、`make test` | 0 / 0 / 0 | `final-{clean,host,test}.{log,exit}` |
| 最后显式 probe ELF / 普通生产 ELF 构建、objdump/nm | 均 0 | `final-probe-elf.*`、`final-production.*`、`final-disassembly-{0,1}.*`、`final-production-symbols.*` |

`make test` 实际依赖 **4 个静态/链接检查 + 15 个 QEMU 目标，18 次 QEMU 启动**：13 个单次目标、s2bounds 3 次、s2perm 2 次。两个 VM 不计两次启动。两轮全量共 36 次；专用连续三轮 6 次；Linux 1 次；权限/停核变异 4 次，本次合计 **47 次 QEMU 启动**。host/历史编译/静态不计启动。

Linux 镜像 hash 前置检查通过；双 VM boot/topology/console PASS，停止 VM1 后 survivor timers `(330,332) → (713,856)` 且 console PASS。没有把合成 SVM 当作真实 Linux gate。

三轮专用启动每个 `s2perm-{0,1}.log.exit` 均 **124**；对保留 raw 单独执行 ordered checker 的 `checker-{0,1}.exit` 均 **0**。故障后进度条数：第 1 轮 199/199，第 2 轮 198/199，第 3 轮 199/199，均从 `00000001` 递增至 `000000c6` 或 `000000c7`。最后全量重跑同样 QEMU 124/124、独立 checker 0/0，故障后 198/199 条进度；见 `final-qemu/` 与 `final-checker-{0,1}.*`。

首轮 `make test` 的专用 raw 路径被后续重复启动覆盖，未冒称另存了该两份 raw；该轮 make 输出/exit 保留，最终 clean 全量重跑的两份 raw 单独归档补齐证据。传统其他 QEMU runner 的 make 日志主要保留检查输出，不等价于每次完整串口转录。

## 原始 RO/XN 诊断与同核进展

独立原始串口中，RO/XN read-ok 各在对应 fault 之前，fault 是唯一一条，之后才出现 VM1/vCPU0/pCPU0 的递增进度。真实日志的原始记录如下（不是跨行拼装）：

```text
[hv] s2 fault: vCPU0 ipa=0x0 fsc=0xf ec=0x24 class=permission vm=0 vcpu=0 pcpu=0 ipa_valid=0 far_valid=1 far=0x40210000 elr=0x40212000 esr=0x93c1804f wnr_valid=1 wnr=1
[hv] s2 fault: vCPU0 ipa=0x0 fsc=0xf ec=0x20 class=permission vm=0 vcpu=0 pcpu=0 ipa_valid=0 far_valid=1 far=0x40211000 elr=0x40211000 esr=0x8200000f wnr_valid=0 wnr=0
```

前者为 store 的 EC24/DFSCf/WnR1；后者为目标入口 fetch 的 EC20/IFSCf。普通 permission 两者均 IPA 无效，不能把日志中的 0 当 IPA。正常运行没有 STORE SURVIVED / XN ENTERED / XN RETURNED。

survivor 源码每轮读取计数器，等待 `freq/20` 后再输出递增值，即约 50 ms；拓扑配置把 VM1 vCPU0 与故障 VM0 vCPU0 放在 pCPU0。checker 不只计数：它要求 fault 后进度大于 fault 前最大值。本次正常 fault 出现在首个 survivor 标记前；合成测试另外验证已有 fault 前进度时的规则。

## 隔离变异：构建成功、真实触发、检测为红

全部变异在 `/tmp/hv012-independent/mutations/{noclass,permissions,halt}/` 的 `git archive HEAD` 快照执行。保存 patch、命令、构建/运行/各 checker 原始 exit，未让真实工作树承载变异。

| 变异 | build | 行为/QEMU | marker checker | ordered checker | 证据 |
|---|---:|---:|---:|---:|---|
| 删除 MMIO 分类准入，恢复 ISV-only 与原始 HPFAR/FAR 地址组合 | 0 | host=1 | — | — | `noclass.patch`、`noclass-{build,behavior}.*`：581 断言、228 失败，最先就是 permission 的返回值/spy次数/寄存器三断言。 |
| RO→RW，同时 XN 清零，fixture 独立叶期望也同步放行；RO 启动 | 0 | 124 | 1 | 1 | `permissions.patch`、`permissions-0-*`：fixture 两 VM checked，RO read ok 后真实 `RO STORE SURVIVED`，无 s2 fault。 |
| 同一权限变异；XN 启动 | 0 | 124 | 1 | 1 | `permissions-1-*`：XN read ok 后真实 `XN ENTERED` 和 `XN RETURNED`，无 s2 fault。 |
| abort report 后 park→无限 WFE 停当前核；RO 启动 | 0 | 124 | 0 | 1 | `halt.patch`、`halt-0-*`：保留上述正确 RO fault，但无任何 survivor 进度。 |
| 同一停核变异；XN 启动 | 0 | 124 | 0 | 1 | `halt-1-*`：保留上述正确 XN fault，但无任何 survivor 进度。 |

权限变异不是拿 fixture 拒绝启动冒充穿透；实际 guest 已打印 store 后及 XN 入口/返回标记。停核变异的旧正负 marker 检查仍为 0，而顺序检查为 1，证明顺序部分承重。

另保留初次仅删除 admission if 的变异 `noclass-gate-only-*`（build0、行为1、33 失败）。它仍使用 classifier 产出的 IPA，不足以单独证明 permission spy 复现，所以最终采用上表恢复原始地址组合的更完整变异。

所有变异源文件已经逐字恢复至 HEAD，并清除三个 archive 的构建产物：`mutations/restoration.txt`、`*-restored-clean.{log,exit}`。真实树从未改生产代码，最终 clean 全量构建/门禁与普通生产 ELF 检查都通过。复现时必须先重新应用保存的 patch。

## 独立合成日志反证与已发现限制

`synthetic/run.py` 从本次真实 fault/read/fixture/progress 记录独立构造 RO/XN 共 30 个输入，`results.json` 保留每例预期和实际；脚本本身 exit0 **不表示这些预期全通过**。

- 两个正向输入通过。
- 每个 probe 的 11 类反向输入全部拒绝：缺 fault、错 FSC、穿透、仅 fault 前进度、错 VM/vCPU/pCPU、错 FAR、错 ELR、两条错误记录拼凑、重复旧 progress。单条 fault 的身份/地址绑定与 `len(faults)==1` 在这些反证下有效。
- 额外 3 类输入每个 probe 均被错误接受，共 6 个假阳性；对这些文件另行执行完整 fixed-marker 与 ordered checker，两者原始 exit 都为 0，见 `*.markers-check.*`、`*.ordered-check.*`。

### 阻塞项

本次受控生产路径、真实故障、真实穿透和同核存活证据中**未发现阻塞项**。这不消除下列 checker 的适用边界；若上层要求对任意外部/被编辑日志也可信，则必须先修复，不能沿用本报告的受控日志结论。

### 非阻塞缺口（真实假阳性，不隐去）

1. **引用/匹配重放可冒充进度**：`check_s2perm_output.py:33` 的 survivor regex 未锚定行首；故障后仅有 `DIAGNOSTIC REPLAY: SVM: s2perm survivor ... progress=...` 也被接受。`synthetic/{ro,xn}-quoted_progress_only.log` 没有真正以 SVM 开头的 fault 后进度行。该数值大于 fault 前最大值；同值旧进度重放则正确拒绝。真实 runner/guest 不产生该引用格式，现有正常和停核日志不依赖此漏洞。
2. **重复 key 后值覆盖**：第 23 行 `dict(re.findall(...))` 接受同一记录 `vm=1 vm=0`，后者覆盖前者。见 `*-duplicate_wrong_fields.log`。这不是已经观察到跨两条记录拼凑；双 fault 拼凑测试实际为红。生产 printk 不生成重复 key。
3. **未交叉解码原始 ESR**：把 raw ESR 改成 `0x0`、仍保留派生 EC/FSC/WnR，checker 仍通过（其自测也使用 esr=0）。见 `*-inconsistent_esr.log`。本次真实 ESR 已人工按位核对一致；单凭 checker 无法证明 logger 的派生字段正确。

这些是测试输入校验/日志来源假设的边界，不是 guest RAM 保护利用证据。建议作为窄范围 checker 加固记录给父架构师处理，不由验收线程擅改实现或扩大本片。

## 还原、交付与未验证范围

- 本线程只新增本报告；未暂存文件、未 commit/push/checkout。生产源码、测试源码与 Makefile 对 HEAD 无工作树差异；父线程的独立文档不由本线程修改。
- `scripts/plugctl.sh` 保留，前后 SHA256 相同；用户 rk3588 路径未修改。初始/最终受保护摘要保存在环境及 final-state 证据中。
- 初次 objdump 时 make 已删除中间 probe ELF，命令失败；随后显式 make ELF 再 objdump，最终两份退出码均 0；未把失败命令算通过。
- 本子线程没有 Linear 工具，未更新/关闭 H-123；请由父线程同步本报告、原始证据和上述非阻塞缺口，保持全项未关闭。

未验证动态 TLBI、LPA2、真机、HSM/DM；这些不在批准范围内。checker 的六个合成假阳性必须随证据保留，不能将本次“受控原始日志通过”宣传为任意日志的完备验证。

## 追加：checker 修复独立验收（`74b2222`，合入 BLOCKED）

### 本轮结论与已批准处置

研发仅修两个 checker 文件后，本线程独立复核并重跑全部要求。**原六个假阳性全部修复，全部运行门禁通过；但新增完整记录反证仍有一项被接受，本轮不得合入 PASS。** 该结论不改写前文 `8d538ef` 曾有六个假阳性的历史。

独立新增 68 例约束边界测试中，XN fault 记录尾部 `wnr=0'` 被接受；完整 `tests/run_s2perm_test.sh` 也返回 0。已经向父角色报告；父裁定它属于本轮“完整 fault 行、不接收引用”的承诺，**是阻塞而非范围豁免**。批准的最窄后续修复是精确核验现有生产日志中的 XN 占位 `wnr=0`（仍为 `wnr_valid=0`），RO 保持 `wnr=1`，并加该回归测试；不是扩展通用日志语法/鉴真框架。本验收线程未修改实现，等待父线程另行交研发处理及最终 HEAD 复验。

### 版本、范围和证据隔离

- 本轮被测 HEAD：`74b2222dcd5b8578b99d5959d64710529906d607`，分支不变；RED：`feeb8fb`。
- 新证据独立保存在 `/tmp/hv012-independent/checker-fix/`，没有覆盖原目录中的历史运行证据。以下路径除特别说明均相对此子目录。
- `git diff --name-only 8d538ef HEAD` 精确只有 `tests/check_s2perm_output.py` 和 `tests/check_s2perm_output_test.py`。独立脚本断言此集合相等，见 `scope-check.txt`、`changed-files.txt`、`full-fix.diff`。其他所有 tracked 文件（生产源码、Makefile、runner、fixture、linker、host 测试）均不变；专项比较 `unchanged-production.exit=0`。
- 使用初验同一环境：GCC 13.3.0、QEMU 9.2.4、指定 PATH/LD_LIBRARY_PATH/CROSS_COMPILE；所有 host/QEMU 目标分命令执行，直接保存退出码。
- 原报告快照 `report-before.md` 留存。本次同时核对 linker 并更正文档笔误：`.stack` 起点 `0x403f0000`，大小 `0x10000`，ASSERT 终点 **`0x40400000`**，故区间为 `[0x403f0000,0x40400000)`；原文上界 `0x4040000` 少了一个零，未改 linker。证据 `stack-check.txt`。

### 修复逻辑独立审阅

- fault/read/progress 的 regex 均加 `^`/`$`、`re.M`，允许 LF/CRLF；read/progress 文本和值的格式固定。引用前缀、前导空格及 read/progress 尾随引用符拒绝。该改动不是仅做字符串出现次数检查。
- fault 按空白分 token、`partition("=")`，所有 key（不只身份 key）一旦重复立即拒绝；相同值重复也拒绝。独立用例遍历实际 fault 的全部 key，并验证未知 key 重复。
- ESR 必须可解析、非负且小于 `2^32`，raw EC/FSC 必须与期望且已打印的字段一致；RO raw WnR 与打印值相同；两探针拒绝 S1PTW，Data Abort 拒绝 CM。Instruction Abort 的 WnR 不作为架构有效写方向解释。
- 单 fault、VM/vCPU/pCPU、FAR/ELR、permission/IPA valid、read 在 fault 前、progress 在 fault 后且大于全部 fault 前值、穿透标记禁止等原约束保留。
- **遗漏点**：XN 的打印占位 `wnr` 未列入 expected，也不参与适用 WnR 的 raw 比较。fault regex 的正文仍允许任意非换行字符，因此末尾 `wnr=0'` 变成一个未验证的值，虽然 regex 匹配到了行尾，记录本身并非合法生产打印格式。

### 独立历史 RED、GREEN 与原语料

历史用 `git archive` 单独展开两个 checker 文件，直接执行各自 Python 自测；未采用研发日志作为结果，也未 checkout 真实树。

| 独立执行 | 原始 exit | 结果/证据 |
|---|---:|---|
| `feeb8fb` 自测 | 1 | `history/feeb8fb-selftest.log`：77 checks，36 failures；正向控制先通过，失败来自行为断言。 |
| `74b2222` 自测 | 0 | `history/74b2222-selftest.log`：77 checks，0 failures。 |
| RED 对原 32 语料 | 1 | `history/feeb8fb-corpus.{log,json}`：32 例、6 个与预期不符，重现原六个假阳性。 |
| GREEN 对同一原 32 语料 | 0 | `history/74b2222-corpus.{log,json}`：32 例、0 个与预期不符；4 正向接受、28 反向拒绝。 |
| 本轮独立新增边界测试 | **1** | `independent-boundaries.{log,json}`：68 例、1 个与预期不符，见下述阻塞。 |

原 32 语料为初验保存的 30 个派生输入及两个 original 实际日志。`recheck-corpus.py` 只读这些原文件，不运行会改写原输入的初验生成脚本。原六例 quoted-progress / duplicate-fields / inconsistent-ESR 在本次最终 checker 下全部拒绝。

独立新增用例没有照抄研发脚本：从原真实记录派生 LF/CRLF 正向，分别测试三种事件的引用前缀、前导空格、尾随引用符，逐一重复所有字段及未知字段，验证 raw EC/FSC/S1PTW/溢出/负值、Data CM/WnR、双合法 fault、旧进度重放。除下述一例外均符合预期；不无限扩展任意外部日志鉴真场景。

### 完整 runner 回放与唯一阻塞复现

`runner-replay.py` 在独立 PATH 中提供专用 `timeout` 替身，只复制指定已保存原始日志并返回 124，然后执行**未改的完整 `tests/run_s2perm_test.sh`**，包括 fixed-marker 和 ordered checker。替身与每次完整命令、退出码保存在 `replay-bin/`、`runner-replay/`；这明确是已保存日志的 runner 回放，**没有启动 QEMU，不计入启动次数**。本轮另有实际 QEMU 门禁，见下一节。

共 37 次回放：原 32 语料、1 个新阻塞、原权限穿透/停核的 4 份真实变异日志。36 次符合预期；唯一不符是新增 XN fault 尾随引用符。原两个真实日志通过、原六修复样例拒绝。证据 `runner-replay.{log,json,exit}`，整体 exit=1 正是暴露该遗漏，不能表述为全绿。

**阻塞输入**：`boundaries/xn-fault-trailing-quote.log`。它保留真实 fixture/read/fault/后续 progress，唯一相关改动是 fault 最后一个字段尾随单引号：

```text
[hv] s2 fault: vCPU0 ipa=0x0 fsc=0xf ec=0x20 class=permission vm=0 vcpu=0 pcpu=0 ipa_valid=0 far_valid=1 far=0x40211000 elr=0x40211000 esr=0x8200000f wnr_valid=0 wnr=0'
```

期望拒绝；实际 `check(...,1)=True`，完整 runner 原始 exit=0。同形 RO、read、progress 尾随引用符均拒绝。完整 runner 反证的详细文件为 `runner-replay/32-xn-fault-trailing-quote.*`。这是 checker 对本轮已承诺格式的遗漏，**不是生产 abort 或 RAM 权限绕过**。

### 当前 HEAD 全部门禁原始结果

出现阻塞后，没有替换被测代码，也没有中断/重用部分构建冒称新版本结果；正在运行的门禁自然结束，以下全部属于 `74b2222`。

| 命令 | 原始 exit | 日志/退出码 |
|---|---:|---|
| `make host-test` | 0 | `host.{log,exit}`，包含 77 项 checker 自测。 |
| `make clean`，然后单独 `make test` | 0 / 0 | `clean.{log,exit}`、`test.{log,exit}`。 |
| `make test-qemu-s2perm` 第 1/2/3 轮 | 0 / 0 / 0 | `s2perm-{1,2,3}.{log,exit}`。 |
| `make test-linux` | 0 | `linux.{log,exit}`、完整串口 `linux-raw.log`。 |
| `make test-x86-stub` | 0 | `x86.{log,exit}`。 |
| `make check-arch-boundary check-offsets check-offsets-target` | 0 | `static.{log,exit}`，三项 PASS。 |

`make test` 仍是 4 项静态/链接检查、15 个 QEMU 目标、18 次启动；随后权限三轮共 6 次，Linux 1 次。本轮共 **25 次真实 QEMU 启动**，未把双 VM 或 37 次日志回放计作额外启动。

全量门禁专用权限 raw 及时保存到 `full-test-raw/`；连续三轮保存到 `round-{1,2,3}/`，不会再被下一轮覆盖。8 次权限 QEMU 的 `.log.exit` 均为 **124**，另行直接运行最终 ordered checker 的 `full-test-checker-{0,1}.exit` 与 `round-{1,2,3}-checker-{0,1}.exit` 均为 **0**。每份真实日志均只有一条正确 fault；每次故障后都有 199 条新进度，从 `00000001` 到 `000000c7`。RO/XN 的原始 ESR、身份、FAR/ELR 与初验一致，见 `qemu-summary.json`。

Linux 镜像 hash 前置检查和双 VM boot/topology/console 均通过；VM1 停止后 survivor timers `(346,248) → (728,623)`，console PASS。所有成功门禁不能覆盖新增反例所形成的合入阻塞。

### 原变异证据继续有效、状态与待办

生产代码、fixture 与真实 runner 相对 `8d538ef` 机械确认未改，故按父批准复用初验的真实分类/权限/停核变异证据，不重新修改源码或重复全部变异。用当前完整 runner 重检原四份 QEMU raw：

| 原日志（原证据目录下） | 当前 runner exit | 保留结论 |
|---|---:|---|
| `mutations/permissions-0-qemu.log` | 1 | RO 真实 STORE SURVIVED，拒绝。 |
| `mutations/permissions-1-qemu.log` | 1 | XN 真实 ENTERED/RETURNED，拒绝。 |
| `mutations/halt-0-qemu.log` | 1 | RO fault 正确、无 survivor，顺序拒绝。 |
| `mutations/halt-1-qemu.log` | 1 | XN fault 正确、无 survivor，顺序拒绝。 |

本轮未施加新变异；旧 archive 还原状态保留。所有构建已经结束，真实树 HEAD 仍 `74b2222`，没有 staged 文件，没有 tracked 未提交源码改动；本线程仅修改原未提交验收报告。`scripts/plugctl.sh` 与 rk3588 受保护文件摘要前后一致，见 `preserved-before.sha256`、`final-state.txt`。

**合入阻塞清单：仅 XN fault 尾随引用符被接受这一项。** 父已批准研发窄修 XN 打印占位期望及对应回归；最终新 HEAD 必须再独立复验后才能给合入 PASS。原六个假阳性已经修复，但本轮不关闭 H-123，也不替代父 reviewer 决定或 Linear 同步。

本报告从不保证任意外部日志的真实性；不支持动态 TLBI、LPA2、真机、HSM/DM。上述新阻塞是在批准的具体记录格式内，不借此无限扩展通用日志鉴真范围。

## 最终追加：XN 占位字段窄修复验（`05954ec`，解除阻塞 / 合入 PASS）

### 最终结论与范围锁

**独立复验通过，上一节唯一阻塞已解除，本切片合入验收 PASS。** 这是 `05954ec92b4ab03a4a81df120d8aed42cc2560a7` 的结论，不回写 `8d538ef` 的六个假阳性或 `74b2222` 的失败历史。当前阻塞清单为空；父 reviewer、发布及 Linear 同步仍按原流程执行，不关闭 H-123。

本轮仅验证父批准的既有契约与已经发现的边界，没有新增任意外部日志鉴真要求。XN 的 `wnr=0` 是既有生产打印占位，仍要求 `wnr_valid=0`，**不赋予 Instruction Abort 的 WnR 架构有效语义**。

新证据根目录：`/tmp/hv012-independent/xn-wnr-final/`。以下相对路径均位于此目录；此前证据及本轮修改前报告 `report-before.md` 保留。

### 机械范围确认及独立先红后绿

- 对 `74b2222..05954ec` 完整差异审阅：只在 checker 的 expected 中统一要求 RO `wnr=1` / XN `wnr=0`，删除原 RO-only 赋值；自测只增加 XN 占位缺失、非零、尾随引用符三个负例。总计两个文件、7 行新增 / 2 行删除。证据 `final-fix.diff`。
- 独立断言 `74b2222..HEAD` 与 `8d538ef..HEAD` 的 changed-path 集合都精确等于两个 checker 文件。生产源码、Makefile、runner、fixture、linker、host 测试及其他 tracked 文件相对 `8d538ef` 均未改变。证据 `scope-check.txt`。
- 临时目录用 HEAD 自测配 `74b2222` 原 checker，随后与精确 HEAD 文件组合比较；没有换真实树代码，也没有仅引用研发的 RED 日志。

| 独立历史组合/检查 | 原始 exit | 实际结果/证据 |
|---|---:|---|
| `74b2222` checker + HEAD 自测 | **1** | `history/red-selftest.log`：80 checks、3 failures，精确为新增 XN 缺失/非零/尾引用；旧正向与回归先通过。 |
| 同一旧 checker 对原 XN 尾引用 CLI | **0（错误接受）** | `history/red-blocker.log`，重现原阻塞。 |
| `05954ec` checker + HEAD 自测 | **0** | `history/green-selftest.log`：80 checks、0 failures。 |
| 最终 checker 对同一原 XN 尾引用 CLI | **1（正确拒绝）** | `history/green-blocker.log`，使用原文件、未修改反例。 |

以上各自 `.command`、`.exit` 与组合源文件保留在 `history/`，RED 是可执行行为失败，不是语法/依赖或构建失败。

### 原语料、68 个边界及完整 runner 复验

`recheck-known.py` 只读原 32 个输入与上轮独立 68 个边界输入，读取 bytes 后解码，保留真实 CRLF，不重跑会改写旧证据的生成器。最终结果：**32/32 与 68/68 全部符合预期**，直接 exit=0；见 `known.{log,exit}`、`known-results.json`。原六个假阳性以及上一节 XN fault 尾随引用符均拒绝。

随后独立执行未经修改的完整 `tests/run_s2perm_test.sh`：通过临时 PATH 中的 timeout 替身回放指定保存日志并返回 124，完整保留 fixed-marker 和 ordered checker 流程。这是明确的**日志回放，不是新 QEMU 启动**。替身、每条命令、原始日志和 exit 在 `replay-bin/`、`runner-final/`；整体 `runner-final.exit=0`。

| 最终完整 runner 回放集合 | 数量 | 结果 |
|---|---:|---|
| 原 32 语料 | 32 | 全部符合既定预期。 |
| 原独立边界 | 68 | 全部符合既定预期，原 `xn-fault-trailing-quote.log` runner exit=1。 |
| 各轮独立归档的正常原始 RO/XN 文件 | 26 | 全部 runner exit=0。 |
| 原真实权限穿透 / 停核日志 | 4 | 全部 runner exit=1。 |

共 **130 个回放检查、0 个预期偏差**，详见 `runner-results.json`。26 份正常 raw 覆盖初验三轮及最终重建、上轮全量及三轮、本轮全量及三轮、原两份 original 取样；含同次启动取样副本，不把文件份数当作独立启动次数。回放也不与真实现场门禁启动数混算。现场门禁结束前的一次 122 项中间回放同样通过，单独保留为 `runner-preliminary.*`，最终结论使用包含全部本轮 raw 的 130 项结果。

完整 runner 对四份初验真实变异证据仍为红：RO `STORE SURVIVED`、XN `ENTERED/RETURNED` 被拒绝；RO/XN 正确 fault 但停核无 survivor 仍被顺序检查拒绝。原生产分类 host 变异及源码还原证明继续引用初验；本轮没有重新施加生产变异。

### 最终 HEAD 现场门禁

使用与前次相同的指定工具链环境，版本与 HEAD 记录于 `environment.txt`。所有目标分命令执行，`gates.sh` 紧随原始命令记录 `$?`，未从管道尾部推断。

| 最终现场命令 | 原始 exit | 日志/退出码 |
|---|---:|---|
| `make host-test` | 0 | `host.{log,exit}`，包含最终 80 项自测。 |
| `make test` | 0 | `test.{log,exit}`。 |
| `make test-qemu-s2perm` 连续第 1/2/3 轮 | 0 / 0 / 0 | `s2perm-{1,2,3}.{log,exit}`。 |
| `make test-linux` | 0 | `linux.{log,exit}`、完整串口 `linux-raw.log`。 |

本轮 `make test` 再次覆盖 4 项静态/链接检查（含 x86 stub）及 15 个 QEMU 目标 / 18 次启动；额外权限三轮 6 次、Linux 1 次，共 **25 次真实 QEMU 启动**。本轮没有额外单独执行 x86/static 命令，也没有为测试期望值这一窄修重复 clean 全构建；上轮 `74b2222` 的 clean 全构建和单独 x86/static exit=0 证据保留、版本明确，不伪称它们在本轮重新执行。

全量门禁及三轮的 8 次真实权限启动都及时保存 raw：`full-test-raw/`、`round-{1,2,3}/`。每个 `.log.exit` 为 **124**，对应独立 ordered checker exit 为 **0**（`full-test-checker-{0,1}.*`、`round-{1,2,3}-checker-{0,1}.*`）。每份日志唯一 fault、正确 read 前置、故障后 199 条新进度，从 `00000001` 到 `000000c7`，没有穿透标记。见 `qemu-summary.json`；原始 RO/XN 记录仍分别为 ESR `0x93c1804f` / `0x8200000f`、EC24/20、FSCf、既定 FAR/ELR 与 VM0/vCPU0/pCPU0，`ipa_valid=0`。

Linux 镜像 hash 校验、双 VM boot/topology/console 通过；停止 VM1 后 survivor timers `(343,305) → (757,742)`，console PASS。这是最终 HEAD 的真实 Linux 运行，不是回放。

### 最终状态与声明上限

所有构建/回放均已结束；真实树 HEAD 保持 `05954ec`，未修改实现、未暂存、未提交、未 push 或切换分支。本线程仅更新未提交验收报告。旧变异 archive 的生产文件再次与最终 HEAD 比较一致；没有新变异需要还原。`scripts/plugctl.sh` 与 rk3588 受保护文件校验和前后一致。状态及一致性审计保存在 `final-state.txt`、`audit.{log,exit}`、`old-mutation-restoration.txt`，新证据清单为 `SHA256SUMS`。另逐项复核初验及上轮证据清单，SHA256 校验均 exit=0（`initial-evidence-preserved.*`、`checker-fix-evidence-preserved.*`），确认没有覆盖历史证据。

**最终阻塞：无。** 已证明批准 profile 的分类/启动前权限及既定日志契约，未证明任意外部日志真实性；动态 TLBI、LPA2、真机、HSM/DM 仍未验证，也不据此关闭 H-123。下一步由父 reviewer 复核最终证据并处理合入及 Linear 同步。
