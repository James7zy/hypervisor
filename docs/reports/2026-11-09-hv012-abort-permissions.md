# HV-012：abort 分类与启动前 RO/XN 交付

> 父架构师关卡②：PASS。最终独立被测代码 `05954ec`，基线 `f31a22b`。
> [设计](../superpowers/specs/2026-11-09-hv012-abort-permissions-design.md) ·
> [计划](../superpowers/plans/2026-11-09-hv012-abort-permissions.md) ·
> [ADR-0027](../adr/0027-abort-classification-and-startup-permissions.md) ·
> [完整独立验收与原始证据](2026-11-09-hv012-abort-permissions-verification.md)

## 交付

ARM abort 先分类、判断地址有效性，再决定是否允许 MMIO。仅支持的 Data translation、
ISV、非 S1PTW/CM、可靠 IPA 可模拟；permission/access-flag/Instruction Abort 不调用
MMIO、不改 GPR/ELR。普通 permission 不盲信 HPFAR，日志显式标 `ipa_valid=0`。

专用 `svm_s2perm` profile 在 VM0 首次激活前使用真实 `s2_protect()` 设置两页 RO/XN。
两次独立 guest 启动先读目标，再尝试写入或取指，要求精确 permission fault、无穿透、
fault 后同 pCPU VM1 有新进展。正常生产 builder、公共 VM 配置与默认 RAM 权限未改变。

| 提交 | 内容 |
|---|---|
| `ba6eb68` | 独立设计复核后的范围锁与验收计划 |
| `7736e8f` | 单次 abort 快照、生产 MMIO 逻辑等行为提取 |
| `ff78ded` | 行为 RED：陈旧 HPFAR 的 permission fault 导致 spy dispatch/GPR/ELR 三项失败 |
| `a94d4bc` | 分类、地址有效性、Instruction Abort 诊断 GREEN |
| `8d538ef` | 启动前权限 fixture、真实 RO/XN probe、顺序 checker |
| `feeb8fb` → `74b2222` | checker 引用/重复字段/ESR 矛盾假阳性 RED→GREEN |
| `05954ec` | 补 XN `wnr=0` 占位精确检查及三项回归；先红后绿证据留存 |

构造的 host MMIO 反例不等同已复现生产 RAM 写保护利用：当前 RAM 没有注册 MMIO handler。

## 独立验收与发现闭环

测试专家先在 `8d538ef` 完成生产行为、历史 RED/GREEN、完整门禁与隔离变异；发现六个
checker 假阳性。父将 checker 限定修复纳入合入条件，`74b2222` 修复后独立复核又发现
XN 尾引用字段仍被接受。该项判为阻塞；`05954ec` 最窄修复后再次独立验收解除。
失败历史与原样例全部保留，不将早期报告改写为“一次全过”。

最终 `05954ec`：

| 检查 | 结果 |
|---|---|
| host 与 checker | `make host-test` exit0；checker 80 项自测全过；生产 abort 581 断言等既有套件通过 |
| 独立已知语料 | 原 32 份、独立 68 个边界全部符合预期，所有已发现假阳性拒绝 |
| 完整 runner 回放 | 130 次符合预期；正常 raw 接受，真实穿透/停核日志拒绝；回放不计作新 QEMU 启动 |
| `make test` | exit0；4 项非 QEMU 检查、15 QEMU 目标/18 次启动 |
| 权限目标三轮 | 各 exit0，共 6 次启动 |
| `make test-linux` | exit0；真实 Linux 双 VM/console/survivor 检查通过 |

最终现场门禁共 25 次 QEMU 启动。当前只有两个 checker 文件相对生产验收 `8d538ef`
变化；`74b2222` 曾 clean 全构建及单独 x86/static 通过，最终窄修未再重复 clean 或生产
变异。原变异证据仍有效，并用最终完整 runner 重检实际穿透/停核日志仍为红。

实际 RO：ESR `0x93c1804f`、EC `0x24`、FSC `0xf`、WnR=1、FAR `0x40210000`、
ELR `0x40212000`。XN：ESR `0x8200000f`、EC `0x20`、FSC `0xf`、FAR/ELR `0x40211000`。
两者均 VM0/vCPU0/pCPU0、`ipa_valid=0`，随后同核 survivor 有新进度。XN 的 `wnr=0`
只是 `wnr_valid=0` 的日志占位，不赋予 Instruction Abort 架构上的 WnR 语义。

## 父架构评审与发布边界

父核对实现与最终窄修、独立报告、版本归属和证据归档；没有冒称亲自重跑专家的全部门禁。
新 ARM 决策保持在架构层；普通生产构建不含权限 fixture；拒绝路径与日志有效性不混淆。
收尾只改文档及归档，不改变被验收实现。H-123 保持 **In Progress**。

未交付：按需补页、运行期 protect/TLBI、VMID 复用、AT、LPA2、真机/cacheability/设备排序、
DMA/SMMU、HSM/DM。checker 证明既定受控日志契约，不是任意外部日志鉴真器；本片不关闭
完整 HV-012，也不替代 H-127 属性验收。
