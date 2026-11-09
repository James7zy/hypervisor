# XL-003 HV 侧 RAM 边界：独立验收

## 结论与范围

**独立测试通过，未发现本切片的阻塞项；仍须父架构师/独立 reviewer 最终评审。**
本报告只验收初始 Stage-2 RAM 边界，不代表 XL-003/H-124 跨 HV/HSM/DM 全项完成。

- 分支：`feature/xl003-ram-bounds`。
- 被测 HEAD：`7a57d7a4a449ab59b9678f92d7060990fa6912c4`；基线：`40db2d4`。
- 批准 spec：`9c65667`；独立比较确认 HEAD 未修改其正文（`approved-spec.exit=0`）。
- 已阅读 CLAUDE、批准 spec/plan，并审查 `40db2d4..HEAD` 全部实现 diff、生产调用者与真实配置。
  研发交接仅作线索；以下结果均为本线程重新执行，不引用研发日志作为通过证据。
- 不提交、不 push、不切分支；没有修改实现、既有 expect、`scripts/plugctl.sh` 或 RK3588 用户文件。
  本线程唯一仓库新增文件为本报告。父线程另写的 ADR 不在本线程改动范围。

## 环境与证据位置

原始命令输出、退出码及复现脚本位于 **`/tmp/xl003-independent/`**（以下日志名均相对此目录）。
父线程发布时将顶层日志、退出码、命令、脚本、输入与审查 diff 归档为
[`artifacts/2026-11-08-xl003-independent.tar.gz`](artifacts/2026-11-08-xl003-independent.tar.gz)，
SHA256：`1cbb1b01666ee5686274dad8824a2fee21fabdd38db04399c01b96fb38054f17`。
不含历史快照、镜像或工具链；旧提交可从 git 重新提取。脚本中的 `/tmp` 路径是本次执行记录，
不是跨机器直接运行保证。

每个表内日志对应同名 `.exit`；多数执行同时保存 `.command`。
`gates.sh`、`history.sh`、`mutations.sh`、`final.sh` 记录完整过程；
`implementation.diff`、`extraction.diff` 保存审查输入，`exit-summary.txt` 汇总退出码。
退出码直接取自命令，不经 `tee` 等管道取末端状态。

```sh
export PATH=/tmp/wrapbin:$PATH
export LD_LIBRARY_PATH=/tmp/tc/usr/lib/x86_64-linux-gnu:/tmp/tc/usr/lib
export CROSS_COMPILE=aarch64-linux-gnu-
```

QEMU 为 `/usr/local/bin/qemu-system-aarch64`，版本 9.2.4。
已核查 gcc/cpp/ld wrapper 为调用 `/tmp/tc` 实际工具的 `exec`，不是伪造通过结果。
直接执行 QEMU 时另设 `HV_NR_CPUS=4`，与 Makefile/板级配置一致。

## 必跑门禁

| 命令 | 退出码 | 原始日志 | 结果 |
|---|---:|---|---|
| `make host-test` | 0 | `host.log` | Linux builder 21061、SVM builder 20047 个断言均零失败；checker 5 项自检通过；原有 host 套件通过 |
| `make clean`，成功后 `make test` | 0 / 0 | `clean.log`、`test.log` | 四项静态 gate；14 个 QEMU 目标、16 次启动，16 个 `ALL PASS` |
| `make test-qemu-s2bounds` 第 1 轮 | 0 | `bounds-1.log` | 三个独立启动全部通过 |
| 同上第 2 轮 | 0 | `bounds-2.log` | 三个独立启动全部通过 |
| 同上第 3 轮 | 0 | `bounds-3.log` | 三个独立启动全部通过 |
| `make test-linux` | 0 | `linux.log`、`linux-raw.log` | 镜像校验、parser 自检、双 VM 启动/拓扑/console、停止后的 survivor timer/console 通过 |
| `make test-x86-stub` | 0 | `x86.log` | 单独重跑通过；此前 clean 后的 `make test` 已实际编译链接 x86 stub |
| `make check-arch-boundary check-offsets check-offsets-target` | 0 | `static.log` | 三项均 PASS |

`make test` 的 14 个目标中只有 s2bounds 启动三次，其余各一次，因此为 16 次，不能混报为 14 次。
上表不包含后续还原验证和单独保存串口原文的额外启动。

## 生产路径与独立 oracle

实际路径为 `vm_init()` → `vm_arch_init()` → `stage2_init()` → `stage2_build()` → `s2_map()`。
`stage2_init()` 传入 `m->config`；builder 直接使用 `mem_base/ram_pa/mem_size`，未截断 SVM PA 低位。
实际范围如下（半开区间）：

| 配置 | RAM IPA | RAM OA/PA |
|---|---|---|
| Linux VM0 | `[0x40000000,0x50000000)`，256 MiB | `[0x80000000,0x90000000)` |
| Linux VM1 | `[0x40000000,0x50000000)`，256 MiB | `[0xc0000000,0xd0000000)` |
| SVM VM0 | `[0x40200000,0x40400000)`，2 MiB | `[0x40200000,0x40400000)` |
| SVM VM1 | `[0x40200000,0x40400000)`，2 MiB | `[0xc0200000,0xc0400000)` |

- 生产 `stage2_build_check()` 逐级遍历所有非零描述符，按 IPA 判定设备/RAM，而非仅筛选 Normal 叶。
  检查叶 OA、完整属性、授权范围、RAM 总覆盖字节数、设备字节数及 GIC/UART 洞；额外 Device 叶也会拒绝。
- host Makefile 实际编译生产 `stage2_build.c`、`s2_table.c` 与真实 `common/vm/vm_configs.c`，分别使用
  Linux 和 SVM/XVM2 双 VM 编译配置；无 fixture 配置或替身 builder。
  host oracle 独立枚举全叶，并检查每 VM RAM OA 不覆盖 `[0x40080000,0x40200000)` HV 保留区或 peer RAM。
  SVM 其他拓扑只改变 affinity，不改变上述 RAM 三元组。
- host 覆盖首尾页/边界外页、设备洞、OA 非取整、L3 跨 L2 边界、L1 大块、非法对齐、零长度、
  NULL 配置、设备 IPA 冲突、池耗尽，以及生产自检对丢叶/坏 OA/坏属性/额外叶的拒绝。
- IPA 39-bit、RAM PA 40-bit 校验先限制起点，再用 `limit - base` 检查长度，避免加法回绕；端点恰等于上限的
  4 KiB 映射可成功。不是将底层 48-bit 描述符容量误当作当前 VTCR 的 RAM PA 上限。
- 40-bit 限制只作用于 RAM 输出地址，不作用于 host 页表虚拟指针。额外使用当前 host 测试的临时打印版本实测
  `root=0x5c356d5dd000`、`pages=0x5c356d5de000`（均大于 `1<<40`），21061 个断言仍通过。
  编译/运行退出码均 0，见 `host-pointers-build.log`、`host-pointers.log`；生产源没有改动。
- 建表失败仍返回原错误，`vm_init()` 在将 vCPU 设为 RUNNABLE 前 `continue`，该 VM 保持 OFF。
  没有新增回滚/重试保证。设备映射三个区间、GIC/UART 洞、loader 地址、SVM 链接地址及旧场景 expect 均未改动。
  ARM builder 位于 `arch/arm64/mmu/`；新增 ARM 逻辑未下沉 generic 层，静态架构门禁通过。

**授权声明上限：**这是当前静态 profile 的全叶验证，不是任意动态 VM 配置的通用 HV/peer PA 授权系统。
生产参数检查不会自行拒绝所有可能落入其他 VM/HV PA 的任意新配置，不能扩大本切片的安全承诺。

## 历史 RED/GREEN 独立复现

通过 `git archive` 将三个提交分别展开到临时目录运行，没有 checkout 或改动主工作树。

| 历史提交 | 独立执行结果 | 证据 |
|---|---|---|
| `cec9d07` 等行为提取 | `make host-test` = 0 | `history-cec9d07-host.log` |
| `1ea66ee` 行为 RED | `make host-test` = 2；两个 builder 二进制已成功编译，分别直接运行均 = 1 | `history-1ea66ee-host.log`、`red-linux.log`、`red-svm.log` |
| `246be0e` GREEN | `make host-test` = 0 | `history-246be0e-host.log` |

RED 的 Linux 为 6156 断言、6 失败；SVM 为 6158 断言、9 失败。
失败包括叶范围、总覆盖、边界后页以及 SVM 边界前页/OA 等行为断言，**不是编译/链接错误**。
源码比较也确认提取保留旧固定 1 GiB、PA 向下取整及配套 `stage2_equiv_check()`，并未提前修复。

## QEMU 负向探针与 checker 承重

先阅读 harness，再以真实 `s2bounds-0.txt` 全部正向标记加上 `s2bounds-forbid.txt` 生成输入。
五个正向标记均 PASS，禁止标记命中，checker 确实返回 **1**；见
`synthetic-forbidden.input`、`synthetic-forbidden.log`、`synthetic-forbidden.exit`。
不是因为缺失 fault 或 survivor 标记才变红。

最终从 clean 构建的主树额外独立启动三次，完整串口日志另存，逐一交给正式 checker：

| 探针 | 实际 fault | QEMU/timeout 退出码 | checker 退出码 | 原始串口与检查日志 |
|---|---|---:|---:|---|
| 0：HV 镜像 IPA | `ipa=0x40080000 fsc=0x6 ec=0x24` | 124 | 0 | `final-probe-0-raw.log`、`final-probe-0-check.log` |
| 1：授权后第一字节 | `ipa=0x40400000 fsc=0x6 ec=0x24` | 124 | 0 | `final-probe-1-raw.log`、`final-probe-1-check.log` |
| 2：peer backing 当作 IPA | `ipa=0xc0200000 fsc=0x5 ec=0x24` | 124 | 0 | `final-probe-2-raw.log`、`final-probe-2-check.log` |

124 是显式 `timeout 10` 结束持续运行的 QEMU，不作为通过依据；harness 原本也容许 QEMU 超时。
通过依据是精确 fault、`SVM: s2bounds SURVIVED` 不存在，以及其后 VM1 hello/done 全部出现。
`CONFIG_SVM_XVM2` 的真实配置将 VM0/VM1 的 vCPU0 都放在 pCPU0；三个原始日志中均为
fault 在前、VM1 hello 和 `SVM HVC: done (x1=0x10001)` 在后。额外顺序/禁止项审计见 `probe-audit.log`。
**不以 VM1 标记单独证明隔离。** 第三项旧版本本来就会 fault，只作回归，不称为修复了旧第三项漏洞。

## 受控变异、链接边界与还原

全部变异在 `/tmp/xl003-independent/mutation/` 的 HEAD 快照中进行；主树实现从未被临时替换。
原文件先备份，退出 trap 精确还原，不使用 `git checkout`，不会覆盖父线程文档。

1. **一致的旧 builder + 旧自检：**仅将该快照的 `stage2_build.c` 替换为 `cec9d07` 原文，保留配套旧自检。
   用真实 vm_configs 编译历史 RED oracle，Linux/SVM 编译均 = 0、执行均 = 1；
   见 `mutation-host-{linux,svm}-build.log`、`mutation-host-{linux,svm}.log`。
   QEMU 镜像构建 = 0（`mutation-old-build.log`）。前两个 probe 实际执行访存，均打印
   `SVM: s2bounds SURVIVED`，并完成 VM1；没有启动自检拒绝、没有预期 fault。
   两次 QEMU/timeout = 124，正式 checker 均 = **1**，日志为
   `mutation-probe-{0,1}-raw.log` / `mutation-probe-{0,1}-check.log`。
   这证明 guest 访问检查承重，不是拿启动前被新自检拦截冒充受控红。
2. **链接栈变异：**恢复 builder 后 clean，实际把 linker 的 boot stack 从 `0x4000` 增到 `0x200000`。
   `make ... all` 返回 **2**，ld 明确报 `HV overlaps SVM RAM backing`；见 `mutation-stack-build.log`。
   ASSERT 位于 `__bss_end` 和 boot stack 之后，以 `__stack_top <= 0x40200000` 覆盖二者。
   linker 注释明确要求与 `BOARD_SVM_MEM_BASE` 同步；当前二者数值一致，不是只限制 text/data。
3. **精确还原：**两份源与备份逐字节 `cmp` 一致；快照再次 clean，host 与三 probe gate 均 = 0，
   见 `mutation-final-clean.log`、`mutation-final-host.log`、`mutation-final-bounds.log`；`mutations.exit=0`。
   再对主树 `make clean`、`make host-test`、`make test-qemu-s2bounds`，全部 = 0，见
   `final-clean.log`、`final-host.log`、`final-bounds.log`。随后执行上表三个原始串口启动。
   两个当前 build 目录均已无变异镜像；额外变异 host 二进制已删除。历史提交快照仅保留作复现证据。

最终主树 `build/test-s2bounds/hypervisor.elf` 的符号（`final-nm.log`，命令 = 0）：

```text
__bss_start = 0x40088000
__bss_end   = 0x400e2dc0
__stack_top = 0x400e6dc0 < 0x40200000
```

新 ELF 的栈顶数值恰好与旧参考产物相同，但本报告依据的是本轮重新链接后的 `nm`，没有沿用旧值。
`final-sha256.log` 记录最终 builder、linker、ELF 摘要；`final-source-diff.exit=0` 确认主树实现/脚本/测试
与 HEAD 一致，`final-staged-diff.exit=0` 确认未暂存文件，`final.exit=0` 确认完整收尾成功。

## 阻塞与非阻塞项

- **阻塞项：无。**所有指定 gate、历史行为 RED、checker 禁止项反证、实际 guest 穿透反证、链接 ASSERT 反证均完成。
- **非阻塞：**原有 `scripts/run-qemu.sh` 的部分注释仍描述 1 GiB Stage-2 块；地址和执行行为未修改，
  本线程未顺手扩范围清理。`vm.c` 原有失败注释仍提 equivalence，自检名称变化不影响 OFF 路径。
- **未验证边界：**真实 RK3588/其他硬件、实际 cacheability、DMA/SMMU、设备白名单、运行期动态映射/TLBI、
  HSM/DM 及跨组件共享页契约不在本次验收内。低地址设备宽映射仍存在，不因 RAM 测试通过而获额外授权结论。
- Linux 成功启动不是恶意 Linux 越界测试；Linux RAM 边界证据来自真实配置的生产 builder 全叶 host oracle。
  x86 只验编译链接 stub，没有运行 x86 虚拟化。
- 本线程无 Linear 工具，未执行 Linear 同步；父线程须按项目要求发布本报告/gate 证据并读回，H-124 不应因本切片关闭。
- 本地 `/tmp` 工作目录不会随报告提交；父线程已将所需顶层文本证据归档（见上方链接及摘要），不包含构建镜像。最终 merge/push 仍由父线程决定。
