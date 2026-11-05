# M11 片 C：跨 VM SYSTEM_OFF 后幸存 vCPU 停摆（定位中）

> 日期 2026-11-05。状态：**已复现、已建检测网（RED），根因未定**。相关：
> [片 C devlog](../devlog/2026-11-04-m11-slice-c-cross-vm-fp.md)、
> ADR-0019、`tests/svm/cases/xvm2.c`。

## 症状（三种表现，同一处）

1. **定时器停摆**：VM1 `poweroff` 后，VM0 在**另一 pCPU**（pCPU1）上的 vCPU1 不再收到自己的
   vtimer 中断，`/proc/interrupts` 的 `arch_timer` 该列不再增长（cpu0 列正常）。
2. **控制台失联**（部分运行）：其后向该 VM 键入字符不再回显；同一时刻 EL2 shell
   （`vm_list`/`vm_console`）仍正常，物理 UART 中断仍被 EL2 收走。
3. **guest 侧 panic**（部分运行）：被 sweep 的 VM 的 secondary vCPU 出现
   `IABT` at VA `0x1`，`pc=lr=1`，紧贴 `[hv] PSCI: VM1 SYSTEM_OFF` 之后约 1 ms。

片 C 之前（`24b31f8`，1:1 拓扑，无跨 VM 共享）同样的门禁用同一内核镜像**不复现**。

## 已建的检测网

`tests/svm/cases/xvm2.c` + profile `svm_xvm2`（VM0 `{0,1}`、VM1 `{0,1}`，与 Linux 演示同为
2×2-on-2），`make test-qemu-xvm2`，已并入 `make test` 聚合（9 → 10 场景）。

判据：VM1 在 +700 ms `SYSTEM_OFF`（其时其 vCPU1 正在 pCPU1 上运行 → 走 kick 解栈），
VM0 的 vCPU1 与它共享 pCPU1；+900 ms 起要求幸存者的自有定时器在**连续 5 个 100 ms 窗口**
里每个都推进。

在 `dev` HEAD 上 **5/5 确定性失败**：

```
PASS: 'XVM2: v0 alive after VM1 SYSTEM_OFF'
FAIL: 'XVM2: v1 kept taking its own timer after VM1 SYSTEM_OFF'
XVM2: CHECK FAILED
```

（单窗口版本只有 1/8 概率失败——一旦被搁死就不会自愈，所以窗口数才是唯一的不确定源。）

## 已收集的证据

Linux 手工门禁（双 VM 2×2-on-2 → VM1 `poweroff -f` → 读 VM0 的 `arch_timer`）在
`/tmp/gate.py`（一次运行约 2.5 min）+ QEMU gdbstub 读 hv 全局状态。

- **hv 状态快照**（SYSTEM_OFF 后 3 s）：`vm1.v0/v1 = OFF`，`vm0.v0 = BLOCKED`，
  `vm0.v1 = RUNNING`（名义上），`percpu[1].cur_vcpu = vm0.v1`，两个队列都空。
- **VTTBR 正确**：切换时 `VTTBR_EL2` 始终是对应 VM 的（VM0 `0x1_0000_4009e000`、
  VM1 `0x2_0000_4009f000`）——排除 Stage-2/VMID 串台。
- **pCPU1 静默**：环形缓冲（256 条：pick / idle / irq / kick / put / block / resume）显示
  最后一条是 `irq 15 (kick) cv=RUNNING`，之后 pCPU1 再无任何记录。
- **guest 在 WFI 陷阱里空转**：QEMU `-d int` 显示 CPU1 反复出现
  `Taking exception 1 [Trapped WFI] ... ELR 0xffff80008086870c` 与 eret，循环不止；
  `handle_wfx` 的 (1) 分支（`vgic_has_pending` 真）**不 block**，直接经 fast path 重入，
  guest 不前进、pCPU 满负荷 → 另一个 VM 的 vCPU 在该 pCPU 上被饿死。
- ~~**+0x80000 之谜**~~（**已澄清，非缺陷**）：`System.map` 是链接地址，而本工程把
  Image 装在 `RAM+0x80000`，guest 运行期 VA = 链接 VA + 0x80000。用
  `make test-qemu-xvm2`/Linux 门禁实测：**每次** WFI 陷阱的 ELR 都是
  `0xffff80008086870c`，即运行期的 `cpu_do_idle` 的 `wfi`，正常启动时也一样。
  该值可用于把 "运行期 VA" 与 `System.map` 对齐（+0x80000）。
- **PSCI CPU_SUSPEND 轨迹**：失败实例中 guest 曾被 eret 到
  `__pa_symbol(cpu_resume)`（IPA `0x410a69b0`，MMU off 的 resume trampoline），
  随后第一次 WFI 陷阱的 ELR 就已经带上了 +0x80000。

## 已排除的假设

| 假设 | 实验 | 结论 |
|---|---|---|
| 内核 Image 过大压到 guest DTB（IPA 0x42000000） | 裁剪内核到 30 MB | 排除（那是我早期的假线索） |
| PPI 27「恢复时强制写物理 Active」是根因 | 去掉该写 | 排除：楔死照旧（只改变表象） |
| 物理 PPI 在虚拟中断仍在服务时重复注入（pending\|active） | 加 `vgic_lr0_active()` 守卫 | 排除：门禁仍失败（守卫本身是合理不变量，未合入） |
| EL2 后台定时器（CNTHP，T4）引入的唤醒路径 | 把 `vtimer_bg_arm()` 改成 no-op | 排除：仍失败 |
| Stage-2 / VMID 串台 | gdb 读 VTTBR | 排除（见上） |
| `idle_window` 早退吃掉唤醒 | 把 WFI 改成「保持 IRQ 屏蔽再 wfi」 | 未定论：改写后 SVM 侧的窗口计数仍可失败 |

## 最新证据（决定性，SVM 侧可 5 s 一次复现）

用 xvm2（`svm_xvm2`，双 pCPU 共享）在 pCPU1 上抓到的现场：

1. **物理侧正常**：pCPU1 的 redistributor 里 `ISENABLER0 bit27 = 1`（PPI 27 使能）、
   `ISPENDR0 = 0`、`ISACTIVER0 = 0`；同时 `CNTV_CTL = 1`、`CNTV_CVAL` 始终约 100 ms 之后
   ——客自己在反复重新武装物理定时器，说明**物理定时器与 PPI 使能都没问题**。
2. **虚拟侧丢中断**：注入时 live LR0 = `0x70a0_001b_0000_001b`
   （state=`0b01` pending、HW=1、vINTID 27 ✓），但**下一次 WFI 陷阱时 live LR0 变成
   `0x30a0_001b_0000_001b`（state=`0b00` invalid）**，`vgic_has_pending` 因此为 0；
   该 vCPU 从头到尾没取到过任何 vINTID 27（计数器 0）。
3. **影子滞后于 live**：同一次陷阱里 `CNTV_CVAL` live = `0x3c0ee4b`，而
   `v->arch.cntv_cval` = `0x35e85e8`（上次 block 时保存的值）→ 该 vCPU 在被唤醒后是经
   **fast path（`prev == v`，自重复 pick）** 直接重入的，**没有走 `vcpu_arch_resume`**，
   所以影子（CNTV/LR/VMCR）不再跟随 live 状态；随后任何一次 `vgic_restore` 都会用
   **陈旧影子覆盖 live**，把刚注入的 pending 位抹掉。

**现场再加一条（决定性）**：注入后第一次 block/put 时的 `vgic_save` 打印为

```
[dbg] save p1 vm0 v1 live0=30a0001b0000001b sh0=70a0001b0000001b
```

即 **live LR0 已回落到 invalid（0x30a0…），而影子仍是 pending（0x70a0…）**，而
`vgic_save` 对 LR0/LR2/LR3 是**无条件用 live 覆盖影子**（只有 LR1 有 publisher-wins 合并）
——于是刚注入、guest 还没取的 pending 被丢掉。同一时刻 guest 已越过 wfi 重新武装了
CNTV（cval 前进），物理电平落下，HW=1 的 LR0 pending 随电平消失。

**据此的根因假设（下一步验证）**：`handle_wfx` case-3 的 BLOCK 之后，唤醒走
`sched_loop` 的自重复 pick 分支（不 resume）；此时 live 状态由 guest 自己推进、影子停滞，
一旦再有 `vcpu_arch_resume(v)`（例如被 kick 后重新 pick），`vgic_restore` 用陈旧影子写回
LR0/CNTV，**吞掉已注入的虚拟中断** → 幸存者的定时器中断永远到不了 guest。
**验证结果**：已确认写者是 `vgic_save`（见上面的 `save … live0=30a0… sh0=70a0…`）。

修法方向（三选一，需关卡①评审）：
1. `vgic_save` 对 LR0 也做「pending 影子 vs invalid live」的合并：影子 pending 且 live
   invalid 时保留影子。风险：guest 已经取过（IAR/EOI 走完，live 也是 invalid）时会重复注入
   一次伪中断——但 vtimer 是电平触发，重复注入会在下一次 EOI 时自然收敛，代价可控。
2. 注入后不再依赖影子：`vgic_restore` 写 LR0 前，若影子 pending 而 live 无效则沿用影子
   （等价于 1，只是位置不同）。
3. 让 WFI-block 的唤醒以「重新注入物理线」为准：resume 前读物理 PPI 状态，电平仍高就重新
   注入（本质是放弃影子，直接跟随物理电平）。
候选 3 与 HW=1 的语义最一致，也最不依赖影子的一致性；但改动面最大。

## 进一步收窄（guest 侧计数）

在 xvm2 里按 aff0 分开计数后：**两个 pCPU 上的幸存 vCPU 都只收到 1 次 vtimer 中断**
（`t0=1`、`by1=1`）——即 `SYSTEM_OFF` 之后的**第一次** tick 正常，**之后再也不来**。
配合上一条（live LR0 已 invalid、影子仍 pending、`vgic_save` 无条件覆盖），现象是：
第一次中断被取走并 EOI（LR 变 invalid），此后每次注入的 pending 都在 guest 取到之前
被丢掉。

未定的最后一环：注入时确实写了 pending（`0x70a0…`），下一次 `vgic_save` 时 live 已
`0x30a0…`。要么是 guest 抢先重新武装 CNTV 让电平落下（HW=1 的 LR 随电平丢 pending），
要么是 ERET 之后 CPU 没把 pending 的 vIRQ 当成立即中断。下一步应打印 **ERET 之前的
live LR0 + 该次 ERET 的 SPSR（PSTATE.I）**，一次运行(5 s)即可判定。

## ERET 前现场（最后一组数据）

在 `sched_loop` 的 `vcpu_arch_run(v)` 之前打印（SVM 侧，pCPU1，sweep 之后）：

```
[dbg] preeret p1 vm0 v1 LR0=70a0001b0000001b AP0=0 AP1=0 vmcr=ff4c000a spsr=345 elr=402002d4
```

- `LR0 = 0x70a0…`：**pending**、HW=1、vINTID 27 —— 虚拟中断确实挂在 LR 里；
- `vmcr = 0xff4c000a`：VENG1 = 1（虚拟 Group-1 使能）、VPMR = 0xff（放行 0xA0）；
- `AP0/AP1 = 0`：虚拟侧没有活动优先级（RPR 空闲），不存在「同优先级被 RPR 掩蔽」；
- `spsr = 0x345`：EL1h、**I = 0（中断未屏蔽）**。

即：**pending + 组使能 + 优先级放行 + PSTATE.I=0，但 CPU 在 eret 后就是不取这个虚拟中断。**
同时 guest 侧统计（每次陷入都计入）显示整场只有 **2 次**中断进入（每 vCPU 各 1 次，
最后一次 intid=1，是 SGI），此后**两个 pCPU 的幸存者都不再收到任何中断**。

已排除：`ICC_EOIR1_EL1` / `ICC_IGRPEN1_EL1` 被 TC 陷阱丢弃（日志里没有任何
`unhandled sysreg C12_C12_*`，QEMU 不 trap 它们）。

## 二分与两个失败修法（避免重复劳动）

用 xvm2 逐提交二分（把 `svm_dual` 的 VM1 亲和强改成 `{0,1}` 后再跑本用例，
脚本 `/tmp/bisect-c.sh`）：

| 提交 | 结果 |
|---|---|
| `24b31f8`（片 C 之前 B2） | 不确定：B2 的 vCPU1 协作语义不同，用例提前 `HC_GUEST_DONE` |
| `9aef9aa`（T1，C 第一个实现提交） | **RED** |
| `7bce7e9`（T1b） | RED |
| `aa50ee4`（T2） | RED |
| `356dcaf`（T6） | RED |

结论：**只要启用跨 VM 共享（C 的 T7 拓扑 + C 的切换路径）就红**，不是某个单点提交引入；
B2 没有可比对的绿基线（它根本不允许共享），所以"最早红"落在 T1。

已试过、**无效**的修法（都实测跑过 xvm2，别再试）：

| 试验 | 结果 |
|---|---|
| `handle_wfx` 的 WFI 改成「保持 IRQ 屏蔽再 wfi」 | 红（1/…) |
| 恢复时不再写 `ELR_EL1`/`SPSR_EL1`（怀疑破坏进行中的 guest 异常返回） | 红 1/3 |
| 注入前加 `vgic_lr0_active()` 守卫（不在服务中重注入） | 红 |
| vtimer 改软件注入（`vgic_inject_sw`）+ 全 deactivate（放弃 HW=1 链接） | **更差**（3/3 红） |
| 物理 PPI 注入后全 deactivate（保留 HW=1） | **更差**（3/3 红） |
| 关掉 EL2 后台定时器（T4 no-op） | 仍红 |

## 决定性实验：guest 直接轮询 IAR 也拿不到

把 VM0 vCPU1 改成**忙等 + 直接轮询 `ICC_IAR1_EL1`**（既不走 `wfi`/block，也不依赖 IRQ 异常），
同时保留 EL2 的注入：

```
XVM2: dbg poll27=0  miss=<非零>  t1=1
```

- `miss` 非零说明轮询确实在跑、接口给出的确实是 1023（spurious），不是被我们模拟吃掉；
- `poll27=0` 说明**第一次中断之后，CPU 接口再也不会把 pending 的 LR0 交出来**；
- EL2 侧同时实测：PPI 27 每 ~100 ms 照常触发、`CNTV_CTL=5`（ENABLE|ISTATUS，未屏蔽）、
  `cvd>0`（已过期）、`ICH_HCR_EL2=0x401`（En=1）、`ICH_VMCR_EL2=0xff4c000a`（VENG1=1）、
  `ICH_AP0R0/AP1R0=0`（无活动优先级）。

推论：**pending 的 HW=1 LR0 被硬件丢弃/不可交付**，最可能是 ADR-0001 的模型在跨 VM 切换下
失效——EL2 只 priority-drop、把**物理线的 Active 状态**留给 guest 通过 LR 链接去 deactivate；
但 EL2 在每次 `vgic_restore` 里又用 `gic_ppi_set_active()` 强制写回影子里的 Active 值，
使"物理线仍 Active ⇒ HW=1 的 LR 不会 pend"这一状态被**永久固化**（`has_pending=0`、
guest 轮询只得到 1023）。

**建议的修法（触及 ADR-0001，需关卡①评审）**：
1. `vgic_restore` 不再写物理 Active（删除 `gic_ppi_set_active` 调用）；
2. 死者的残留 Active 改为**拆除时清理一次**：SYSTEM_OFF sweep / CPU_OFF 时，若该 vCPU 的
   live LR0 处于 active 且指向 vtimer，就在其 pCPU 上 deactivate 物理 PPI 27 一次；
3. 用 xvm2 + 手工 Linux 门禁验证（计时器 + 控制台两处）。

已试过并排除（含本轮）：忙等（不 block）仍红 → 排除 block/wake 路径；直接轮询 IAR 拿到 1023
→ 排除 IRQ 异常/向量路径；正是这两条把矛头指向"物理 Active 被固化"。

## 待验证的最后一环（按可能性）

1. **HW=1 LR 的 pending 位与物理线的联动**：注入后 guest 若重新武装 CNTV，物理电平落下，
   硬件会自行清掉 pending——我们看到的 `save` 现场（live invalid / shadow pending）正是这一
   形态。需要打印「eret 之后紧接的那条 ERET 的 SPSR + 第一条 guest 指令是否就是
   `wfi` 之后的指令」，判断 guest 是否根本没被给到取中断的机会。
2. **虚拟侧 ISENABLER(27) 状态漂移**：EL2 的 vGIC MMIO 模型与硬件虚拟接口对 PPI 27 的
   enable/active 记账是否一致（guest 只 enable 一次，此后 EL2 若把该位当成 0，
   HW=1 的 LR 可以 pending 但永不投递）。
3. 用参考实现对照（`../xvisor` 的 `arch/arm64/vgic/vgic-v3.c` 与 bao 的 HW=1 LR 处理），
   确认我们的「只 priority-drop、不 deactivate」与「每次物理 PPI 都重注入」这两条组合
   是否本身就违反了 HW LR 的语义（ADR-0001 的假设需要重新审视）。

## 下一步（按优先级）

1. **吃透 +0x80000**：给 guest 一个必然触发的 WFI（如刚起机的 early idle），打印陷阱
   ELR 并与 `System.map` 对照。若**每次** WFI 陷阱都带 +0x80000，则 `elr_el2` 的
   IPA↔VA 假设本身有误，需回到 guest 的 `kimage_voffset`；若**只有暂停/恢复后**才带，
   则问题在 `psci_cpu_suspend` 的帧保存/恢复（`regs.elr_el2` 被写入过）。
2. 在 `handle_wfx` 的 (1) 分支加一次性诊断：当 `vgic_has_pending` 为真时读 LR0..3 的
   state 位，看究竟哪个 LR 长期 pending（若是 LR0 active+pending，则回到注入协议）。
3. **WFI 空转的直接证据**：`handle_wfx` 的 (1) 分支（`vgic_has_pending` 为真）不 block，
   直接经 fast path 重入。把当时 live LR 打出来看过（正常 idle 时
   `LR0 = 0x30a0_001b_0000_001b`，即 vINTID 27、HW=1、state=pending|active），
   说明"虚拟中断 pending 但 guest 拿不到（active 掩蔽）"会让 guest 的 wfi 永远走
   fast path：guest 不前进、pCPU 满负荷。**下一步要抓的是冻结实例里究竟是哪个 LR
   长期 pending**（LR0 active+pending 还是 LR1/LR2 残留），用一次性诊断（陷阱第 N 次
   后打印四只 LR + ICH_HCR/VMCR）。
4. 用 xvm2 做二分：`git revert` 单片 C 的提交（T1/T1b/T2/T3/T4/T6）逐个跑
   `make test-qemu-xvm2`（约 5 s/次），找出引入点。
5. 修复后用同一 xvm2 转绿 + `make test` 10 场景 + 手工 Linux 门禁连续 3 次全绿，
   再进测试专家复验与架构师关卡②。

## 排查中被自己插桩坑过一次（记下来）

一次性诊断里用 `S3_4_C12_C8_n` 读 ICH_LR，实际那是 **ICH_AP0Rn**（LR 是 `C12_C12_n`）；
cortex-a72 只实现 AP0R0，读 AP0R1–3 触发 EL2 未定义指令 → `!VEC ESR=0x2000000`
并停在 panic_vector → 表现为"双 guest 起不来"。**用符号名 `ICH_LR0_EL2` 等，不要手写
S3 编码。**

## 复现环境（本机）

```sh
TC=/home/corsair/Music/toolchain/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin
PATH=$TC:$PATH make CROSS_COMPILE=aarch64-none-linux-gnu- test-qemu-xvm2   # 5 s
```

Linux 手工门禁需要 guest Image（< 32 MB，否则压到 DTB）、静态 busybox initramfs 与
`/tmp/gate.py` 驱动（本机 `/tmp` 制品，非仓库内容）。
