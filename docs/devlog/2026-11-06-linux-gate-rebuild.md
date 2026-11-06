# Linux gate 重建与复验（2026-11-06）

> 状态：复验完成，三 gate 全绿。本文记录从零重建 Linux gate 的步骤与 `poweroff -f` 陷阱，
> 供后续复验复用；本轮复验结论与已知限制见
> [本轮报告](../reports/2026-11-06-m11-c-reverification.md)。
> 关联：[当前架构](../reference/arm/current-architecture.md)、
> [M11 片 C 回归修复 devlog](2026-11-05-m11-c-regression-fix.md)。

## 是什么

Linux gate 是 `make test` 之外的第二道硬件验收：用未改动的 arm64 Linux `Image` + BusyBox
initramfs 起两个真实 VM，验证 2×2-on-2 共享拓扑下 vtimer、跨 VM SYSTEM_OFF 与幸存 VM 存活。
`make test` 只覆盖裸金属 SVM 场景；Linux gate 覆盖真实 guest 内核路径（PSCI SYSTEM_OFF 经 HVC 陷阱）。

## 前置构建（镜像均为构建产物，不入库）

1. Linux `Image`（内核源 `../linux-6.12.103/`，配置 `guest/linux-6.12-qemu_virt.config`）：

   ```sh
   cp guest/linux-6.12-qemu_virt.config ../linux-6.12.103/.config
   cd ../linux-6.12.103
   make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
   make -j"$(nproc)" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- Image
   ```

2. BusyBox initramfs（`../busybox/busybox` 已构建，applet 集合见其 `.config`）：

   ```sh
   mkdir -p /tmp/ir/{bin,dev,proc,sys,etc,tmp}
   cp ../busybox/busybox /tmp/ir/bin/busybox
   cd /tmp/ir/bin && for a in sh ash hush mount cat echo mdev mknod sync poweroff reboot sleep dd ls ps; do ln -sf busybox "$a"; done
   cd /tmp/ir && cp <repo>/guest/initramfs-init.sh init && chmod +x init
   find . | cpio -o -H newc | gzip -9 > /tmp/initramfs.cpio.gz
   ```

3. guest DTB + hypervisor ELF：

   ```sh
   make guest     # build/guest/guest.dtb + guest-vm1.dtb（psci method=hvc）
   make all       # build/hypervisor.elf（HV_GUEST=linux）
   ```

## 怎么工作（gate 序列）

控制台协议：物理 RX 由 EL2 仲裁——初始焦点 VM0；Ctrl-T（`0x14`）切换 EL2 shell；
`vm_console <n>` 设焦点并退回 guest；TX 不仲裁，两个 VM 输出直达物理 UART。

```mermaid
sequenceDiagram
    participant D as gate driver
    participant H as EL2 shell
    participant G0 as VM0
    participant G1 as VM1
    D->>D: sleep 60 等双 VM 到 shell
    D->>G0: cat /proc/interrupts（timer 快照 T0）
    D->>H: Ctrl-T(0x14)
    D->>H: vm_console 1
    D->>G1: cat /proc/interrupts
    D->>H: Ctrl-T; vm_list（关机前：两 VM on）
    D->>H: vm_console 1
    D->>G1: poweroff -f
    G1->>H: PSCI SYSTEM_OFF（HVC）→ 全 vCPU 停
    D->>H: Ctrl-T; vm_list（VM1 halted）
    D->>H: vm_console 0
    D->>G0: cat /proc/interrupts（T1）+ echo VM0_ALIVE
```

## poweroff -f 陷阱（关键）

busybox 的 `poweroff` 不带 `-f` 时**不会调 `reboot(RB_POWER_OFF)`**，而是向 PID 1 发
`SIGUSR2`（`init/halt.c`：`kill(1, signals[which])`，poweroff 对应 `signals[1]=SIGUSR2`）。
本工程 PID 1 是 `exec /bin/sh` 出来的 busybox shell，不处理该信号 → `poweroff` 静默返回、
VM 不关机。必须 `poweroff -f` 才走 `reboot()` → `kernel_power_off()` → PSCI SYSTEM_OFF（HVC）。

## 验证证据

| 检查项 | 证据 |
|---|---|
| 双 VM 启动 | `busybox rootfs up — vm0/vm1 interactive shell` |
| 拓扑 | `vm_list`：`0  0-1 on` / `1  0-1 on` |
| timer | VM0 arch_timer `(125,109)→(142,117)`；VM1 `(124,125)` |
| SYSTEM_OFF | `poweroff -f` → `reboot: Power down` → `[hv] PSCI: VM1 SYSTEM_OFF - stopping all vCPUs` |
| VM1 停机 | 关机后 `vm_list`：`1  0-1 halted` |
| VM0 存活 | 关机后 `echo VM0_ALIVE` → `VM0_ALIVE` |
