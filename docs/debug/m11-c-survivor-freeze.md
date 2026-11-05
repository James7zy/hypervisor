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
