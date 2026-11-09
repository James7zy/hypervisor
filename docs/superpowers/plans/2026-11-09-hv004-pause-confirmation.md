# HV-004 暂停执行确认实施计划

[设计](../specs/2026-11-09-hv004-pause-confirmation-design.md)，基线 `3b8b9a8`。
T1 同源 RED 已独立验证、软件证明有条件通过，父批准spec最小HCR修订及T2实施；
不是实现验收，硬件观察仍须补齐。不将执行确认扩大为资源回收；两工作日时间盒不顺延。

## T1：协议落点与行为 RED

- 查全 vm_pause/resume、sched_loop entry路径、kick/idle、PSCI wake/CPU_ON 与QUERY调用者。
- 落实spec锁定的packed atomic state/单写者ack/双读QUERY及耗尽busy策略，不重新选择框架。
  按RUNNING快路径、pick→load、self-repick、idle逐项写“ack后不再入guest”交错证明；
  特别覆盖RESUME先扫描RUNNING、随后本核YIELD必须入队，以及两个idle kick窗口。
- 核对ARM WFI wake-up/PMR与GICv3 SGI发布排序，附可定位来源及适用前提。
  固定sleep路径`dsb sy; wfi`（IRQ屏蔽）；执行或跳过后均在idle_window内
  `daifclr #2; isb; daifset #2`；kick发布用`dsb ish`后写SGI。核对全部公共/直接kick调用者。
  T1证明/行为RED交回独立复核后再进入T2 GREEN，不将设计方向PASS冒充具体协议验证。
- 将需要的纯协议逻辑保持在既有VM/调度边界，以最小提取供host执行；不造测试模型替代生产。
- host测试先证明缺少完成协议的行为RED并提交；新ABI的低位/权限必须保持旧行为。
  新符号无法编译不能作为协议RED的唯一证据。

## T2：代次确认与 ABI GREEN

- 两个既有ARM初始化入口在EL2 MMU前写HCR HOST_BASE=0x80000038并ISB、保持DAIF屏蔽；
  现有sysreg.h收敛bit/组合，guest reset使用GUEST_BASE=0x800c6039，保持原guest行为。
  不增加公共hook/每次idle切HCR；核对CPU_ON/run shadow链和全部关键位前提。
- 初始化 per-VM 暂停代次和每 affinity pCPU确认状态；保留 vm_is_paused()公共读入口。
- PAUSE请求所有不同affinity核经过安全点，不靠RUNNING采样推断完成；各核只能确认自己。
  kick处理检查本核任一相关VM待确认，不局限当前owner；非idle请求unwind，idle窗口内不从旧帧解栈。
- 覆盖所有guest-entry路径；暂停排除RUNNING时YIELD成功后无条件幂等入队，再发布ack。
  pick重新读取暂停状态，保持prev/cur_vcpu/launched硬件归属。
- idle最终队列/待确认检查到WFI间IRQ屏蔽，返回/跳过后在idle_window内开放并处理IRQ，
  再屏蔽回安全点；ARM kick保证请求发布先于SGI。同步最小hook契约，不持锁等待/idle/入guest。
- QUERY bit32/33为稳定同代次观察；版本0.2；同步头定义、host/hcall消费者。
- RESUME可取消未完成暂停，清除完成观察、保留RUNNABLE/ON_PENDING队列规则与BLOCKED语义。
- 跑host确定性交错矩阵与受控坏实现判红，GREEN后提交；已有QEMU调度/ABI门禁回归。

## T3：实际控制消费者与 QMP 进度验证

- 复用svm_dual/svm_xvm2；专用SVM控制与目标guest，启动目标两个vCPU。
- 固定且授权内的计数页/Service只读阶段记录，linker校验；不改生产RAM布局或新增共享页。
  Service自主有界推进并以稳定序号发布HOLD；runner不写mailbox，同一HOLD期间控制线程不resume。
- 最小runner复用启动入口和QEMU_EXTRA_ARGS，加QMP socket；匹配ID/异步事件、检查运行状态，
  不发送stop/cont。将采样夹在同一HOLD序号，验证前后双计数增长、HOLD双计数静止且Service持续增长。
- 冻结三个场景：dual无timer OFF-secondary→CPU_ON双核闭环；xvm2共享/定时阻塞唤醒闭环；
  共享核并发控制/CPU_ON/拒绝路径（不混HOLD oracle）。每场景三轮，九次专用启动，不做笛卡尔积。
  保存QMP原回复；解析/阶段观察自检覆盖错ID、异步事件、错阶段、缺失/超时/错误/截断/全机停止。
- 将HCR实际证据嵌入上述场景：从未guest-entry/无timer核的初始HCR/DAIF/GIC条件及
  唯一kick→本核同代ack，guest首次run后路由不回退；boot无候选路径同步覆盖。
  测试观察不增加生产ABI，不能让诊断timer为无timer窗口提供兜底。
- 专用目标纳入make test；统计目标数与实际启动数，不以轮询次数当QEMU启动。

## T4：独立验收及发布

- 研发保留行为RED/GREEN、确定性交错/变异及完整gate原始exit/log，安全checkpoint提交；不push。
- 测试专家独立审阅代次与entry证明，重跑host、专用目标三轮、clean完整test、Linux与静态检查；
  对完成=paused/无RUNNING/遗漏核/旧ack/绕过entry门等反证逐项给证据，不仅引用研发记录。
- 父架构师写新ADR补充ADR-0023，准确区分请求、执行确认、资源静止；报告与最小架构同步。
- 通过后线性合回dev、push dev:develop，H-107同步状态/提交/报告/证据并读回，不关闭全项。

## 必须同步清单

- `include/vm.h`、`common/vm/vm.c`：状态与初始化；必要的最小同源协议实现。
- `include/sched.h`、`common/sched/sched.c`：安全点/entry/请求与恢复。
- `include/hypercall.h`、`common/hypercall/hypercall.c`：QUERY位、版本、权限保持。
- `arch/arm64/irq/irq_handler.c`：共享核任一相关VM待确认时kick请求unwind，保留idle-window防护。
- `arch/arm64/include/arch/sysreg.h`、`cpu/secondary.c`、`guest/vm.c`及`cpu/cpu.c`：
  已批准的HCR常量/两个初始化写点/guest shadow收敛；核对head.S与run汇编，不扩大boot重构。
- `include/cpu.h`、ARM `cpu/cpu.c`：idle/IRQ同步与kick发布屏障的最小契约；复核所有kick调用者。
  x86 stub保持对应接口，无新增公共barrier抽象。
- host sched/hypercall测试与替身、新协议交错测试；SVM hcall期望、专用控制/计数guest/linker/runner。
- Makefile测试目标及必要对象；若新增arch hook必须同步x86 stub，优先避免新增。
- spec/plan/新ADR/验收报告。shell仍表示请求暂停，无需额外UI；Stage-2/设备teardown不在本片。
