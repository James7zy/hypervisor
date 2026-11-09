# HV-012 abort 分类与 RO/XN 实施计划

[设计](../specs/2026-11-09-hv012-abort-permissions-design.md)，基线 `f31a22b`。
设计独立复核通过后实施；两工作日范围锁以 spec 为准。

## T1：规范核对、等行为提取与行为 RED

- 核对全部 abort/MMIO 调用者、ARM 寄存器规则与当前 CPU/VTCR 特性；不将本地实现当规范。
- 从寄存器读取中提取最小 ARM 私有生产决策供 host 编译；普通路径先保持旧行为。
  不改 generic MMIO bus，不把 ARM syndrome 下沉 common。
- host 用 spy handler 构造 permission+ISV=1+指向 MMIO 的陈旧 HPFAR；断言无 dispatch、
  GPR/ELR不变。先确认旧行为确实导致断言失败，单独 RED commit；不得以缺符号充当 RED。
- 记录 access-flag、S1PTW、未知编码/地址、ISV=0、Instruction Abort 的覆盖与正向控制。

## T2：分类、地址有效性与诊断 GREEN

- 应用受支持 FSC 白名单及 MMIO 准入（含显式 CM=0），读取一次 abort 快照；非法路径不得修改 guest 寄存器。
- Permission 不信任 HPFAR；未知完整 IPA 显式标记，不添加 AT 重建。FnV 按类别解释。
- 增加 lower-EL Instruction Abort 分支、可靠 FAR/ELR/EC/FSC 与 VM/vCPU/pCPU 诊断；
  保留现有只 park 故障 vCPU 的控制流。
- `make host-test` GREEN；普通 MMIO、s2fault/s2bounds 与 Linux 回归不放宽原检查。

## T3：编译隔离 fixture 与真实权限门禁

- 新 `svm_s2perm` build profile，复用 xvm2 拓扑；测试源码提供启动前 VM0 页级 fixture。
- 整段授权 RAM 用已有 API 重建 4 KiB 页，原自检先过，再用 `s2_protect` 设置 RO/XN；
  fixture 单独检查只有两页权限不同。不新增 vm_config 权限字段、不改变生产 RAM builder。
- 专用 guest linker 保留探针页与固定 fault 指令地址，断言与代码/数据/栈互不重叠。
- 两次独立 probe：先验证可读，再尝试 RO 写入或 XN 跳转；检查准确 syndrome、FAR/ELR、
  无穿透与 fault 后同核 survivor 新进度（可用专用递增进度 guest，不依赖原 VM1 最后结束）。
  顺序检查复用现有正/负 checker；故障身份/FAR/ELR 绑定同一记录，补合成日志自检。
- 新目标纳入 `make test`，明确目标数与启动数；重复三轮。新增 profile 的产物与生产隔离。
- 做 host 门禁移除、权限放开实际穿透、park→halt 三类变异；记录原始退出码与日志，精确
  恢复后 clean 重建。启动前自检拦截不能代替实际访问反证。

## T4：独立验收、架构关卡与发布

- 研发交回 RED/GREEN commits、命令/原始 exit/log、变异还原状态，不 push。
- 独立专家复核规范适用范围及真实路径，重跑 host/test/Linux/x86/静态检查、权限目标三轮，
  分开报告实现问题、缺失证据与未支持范围。
- 父架构师复核、新 ADR/报告及最小当前架构同步；证据归档，文档和注释不得扩大已测结论。
- 验收通过才 fast-forward 到 dev 并 push dev:develop；同步 H-123 并读回，保持全项 In Progress。

## 必须同步的范围

- ARM `vmexit/data_abort.c,h`、`vmexit.c`，必要的 ARM 私有纯逻辑文件与对象列表。
- `stage2.c` 的编译条件化 fixture 接点；不改生产默认属性或公共配置结构。
- 顶层 Makefile profile/host/QEMU 目标；tests/host abort 行为测试及 s2 protect 断言。
- tests/svm fixture、探针、专用 linker、expect/forbid；顺序 checker 与自检。
- 若 fault 日志变化影响 s2fault/s2bounds，保留并同步精确原有检查，不删除覆盖。
- 新 spec/plan/ADR/验收报告。其余缺口登记为后续，不扩本片。
