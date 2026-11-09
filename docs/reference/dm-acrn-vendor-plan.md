# ACRN devicemodel 移植清单（取/弃/重写）

> **状态**：清单（2026-11-08）。**依据**：[ADR-0024](../adr/0024-device-model-reuse-acrn-contract.md)。
> **上游钉点**：`../acrn-hypervisor` @ `883bc6c3c62ee9f3f7faf5c89273d6c7d3f18dc5`（2026-08-06）。
> **许可**：ACRN 为 `BSD-3-Clause`。vendor 前必须先定本仓库 LICENSE（现在仍是 `TBD`），
> 并保留原始版权头 + `third_party/acrn/NOTICE`。
> **顺序**：本清单**不是开工令**——io_req 环 + HSM 契约（H-148）落地前不 vendor 任何代码。

## 取（数据面，约 5.6k 行）

| 文件 | 行数 | 取的理由 | 需要剥离的耦合 |
|---|---|---|---|
| `hw/pci/virtio/virtio_block.c` | 932 | virtio-blk 数据面（请求解析、队列推进） | `pci_vdev` 出现 5 次，全在 init/glue |
| `hw/pci/virtio/virtio_net.c` | 1274 | virtio-net 数据面 + tap/vhost 后端 | `pci_vdev` 3 次 |
| `hw/pci/virtio/virtio_console.c` | 1214 | virtio-console（我们要的 console 后端） | `pci_vdev` 3 次 |
| `hw/pci/virtio/virtio_rnd.c` | 529 | virtio-rng（最小、最适合当第一个跑通的设备） | `pci_vdev` 3 次 |
| `hw/pci/virtio/virtio.c` 的**队列部分** | ~800 / 2088 | `vq` 管理、`virtio_vq_info`、通知与中断注入 | PCI 配置空间/IO bar/中断路由（x86 MSI-X）|
| `hw/block_if.c` | 1976 | 块设备后端（raw/qcow2） | 依赖 libc/libz，用户态可用；无 arch 假设 |
| `core/iothread.c`、`core/mevent.c`、`core/sbuf.c` | 946 | 事件循环、iothread、共享缓冲 | 无 |
| `include/virtio.h` | 762 | 数据结构与 ops 表（**契约本体**） | 去掉 PCI 相关成员 |

**关键事实**：`pci_vdev` 只在各后端出现 3–5 次（init 签名与少量引用），数据面用的是
`struct virtio_vq_info`（每文件 7+ 次）。所以"数据面可复用、传输层必须重写"是可行的，
不是空话。

## 弃（x86 世界，约 40k+ 行，**不拷贝**）

| 路径 | 原因 |
|---|---|
| `hw/platform/{acpi, atkbdc, cmos_io, ioapic, pit, rtc, hpet, ps2kbd, ps2mouse, usb_mouse, usb_pmapper}` | x86 中断/固件/PS2/USB 主机模型；ARM64 用 DT + GIC（在 EL2）|
| `hw/pci/{hostbridge, lpc, passthrough, gvt, xhci, ahci, npk, ptm, gsi_sharing, platform_gsi_info, wdt_i6300esb}` | x86 PCI 主机桥与直通 |
| `core/sw_load_{bzimage, ovmf, vsbl}.c`、`bios/`、`core/mptbl.c`、`core/post.c` | x86 启动协议/固件；ARM64 直接用我们已有的 `guest/qemu_virt.dts` 生成链 |
| `arch/x86/` | 就是"抛开 Intel 架构"的那一层 |
| `hw/vga.c`、`vdisplay_sdl.c`、`hw/platform/tpm`、`hw/platform/rpmb`、`hw/pci/virtio/{virtio_gpu, virtio_input, virtio_audio, virtio_hdcp, virtio_hyper_dmabuf, virtio_i2c, virtio_ipu, virtio_mei, virtio_gpio, virtio_kernel, virtio_coreu, virtio_rpmb, virtio_vsock, vhost*}` | 没有消费者（无 GPU/音频/IPU/RPMB 需求）；需要时按 OCP 再 vendor |

## 重写（我们自己的部分，约 600–900 行）

| 模块 | 内容 |
|---|---|
| virtio-**mmio** 传输 | `virtio.c` 的 PCI 配置空间/IO bar 换成 MMIO 寄存器窗口（`0x00 magic … 0x100 config`）；ARM64 没有 PCI 总线，第一个传输必须是 mmio |
| `vmctx`（VM 上下文） | ACRN 的 `core/vmmapi.c`（763 行）换成我们的：ioctl 到 `/dev/hv_hsm`（契约取 ACRN 子集）、映射 guest 内存、io_req 环轮询 |
| DT 加载 | 用现有 `guest/qemu_virt.dts` 生成链，替掉 `sw_load_*` + `mptbl.c` |
| 设备注册表 | 只注册我们有的设备（console/rng/blk），不抄 ACRN 的 PCI 设备表 |

## 移除（vendoring 之后必须做的一次性动作）

1. 拷入 `third_party/acrn/` 后，**删掉每个文件里的 `pci_vdev`/`pci_*` 引用**（init 签名、
   `virtio_linkup` 的 pci 参数、MSI-X 相关分支），改成 `struct hv_vdev *`。
2. **删掉 `virtio.c` 的 PCI 配置空间与 IO bar 代码**，只留 `vq` 管理与通知。
3. **删掉所有 `acpi_*` / `mptbl` / `sw_load` 调用点**（数据面文件里通常没有，若命中即说明
   取错了文件）。
4. 保留文件头版权与 SPDX，追加一行 `Ported from ACRN <commit>, see NOTICE`。
5. `git grep -n "pci_\|acpi_\|x86"` 在 `third_party/acrn/` 下必须只剩注释——这条作为
   vendor 提交的机械验收（与 `check-arch-boundary` 同性质）。

## 触发与前置

- **前置（硬）**：LICENSE 决定；io_req/HSM 契约（H-148）；VM 创建/销毁（HV-004 余下）；
  Stage-2 页分配器（HV-011）。
- **第一个设备**：virtio-rng（529 行、无后端进程、无需块设备镜像），它跑通即证明
  "契约 + 传输 + 队列 + 中断注入"整条链路成立。
- **ACRN 上游漂移**：每次 vendor/更新都钉 commit 并写进 NOTICE；与 guest 镜像的哈希纪律一致。
