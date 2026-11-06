# ARM64 CAS 与独占访问同步

> 知识点笔记：AArch64 上"比较并交换"到底怎么实现、什么时候会静默失败，以及本仓库为什么用
> 手写 `LDXR/STXR` 而不是 `CAS`。规格细节回查 ARM ARM（DDI0487）对应章节，本文只记结论和坑。

## 一句话

`CAS` 是 ARMv8.1 LSE 扩展的单指令 compare-and-swap；没有 LSE 时只能用 `LDXR`+`STXR` 重试循环模拟。
两条路径都建立在 **exclusive monitor** 之上，因此都有"配对访问、地址对齐、不被异常打断"的硬约束；
并且**原子性不等于顺序性**——需要 acquire/release 时得额外带后缀或 barrier。

## 两条实现路径

| | LSE（ARMv8.1+） | exclusives（ARMv8.0 基线） |
| --- | --- | --- |
| 指令 | `CAS` `CASA` `CASL` `CASAL`，以及 `SWP` `LDADD` `LDCLR` … 全套 RMW | `LDXR/LDAXR` + `STXR/STLXR` |
| 原子粒度 | 1/2/4/8 字节，需自然对齐；16 字节需 `LSE` 的 `CASP`（成对寄存器） | 1–8 字节；16 字节只能 `LDXP/STXP`，必须 16 字节对齐 |
| 失败方式 | 不失败，硬件保证原子 | `STXR` 返回 1 表示失败，**必须重试** |
| 被打断 | 无所谓 | 中间发生异常/`CLREX`/另一个 exclusive 序列 → monitor 失效，重试 |
| 代码体积 | 一条指令，可内联 | 循环，循环体不能随便插东西 |

exclusive monitor 是 **per-PE 的（地址, 大小）单槽状态**：同一 PE 同时只能有一组独占监视。
嵌套 exclusive（`LDXR` 里再 `LDXR`）会丢掉外层；异常进 EL2 再返回会清掉 monitor，所以
"`LDXR` 之后 printk 一下再 `STXR`"永远失败——这类 bug 只在真机上暴露，QEMU 常常照样成功。

## 内存序：CAS 本身是 relaxed

- 裸 `CAS` **不带**任何顺序：它保证"这一步原子"，不保证前后访存的可见顺序。
- 变体后缀：`A` = acquire，`L` = release，`AL` = acq_rel。`CASA`（acquire RMW）、`CASL`（release RMW）、
  `CASAL`（acq_rel）才构成完整的锁语义。
- `LDXR/STXR` 同理：`LDAXR` 带 acquire，`STLXR` 带 release；**relaxed 版本只够做计数器**。
- RMW 不能用 `LDAR`+`STLR` 手工拼，那不是原子对；要 RMW 就得 `CAS*` 或 `LDXR/STXR`。
- `DMB ISH` 只保证**顺序**，不保证**完成**：跨核"写完数据再发 kick/中断"要用 `DSB ISH`，
  否则中断可能先于数据到达（本仓库 `vpsci.c` 的 CPU_ON 就是 `dmb ish` 发布寄存器 → `dsb ish` → kick）。

## 本仓库怎么用

**`hypervisor/arch/arm64/include/arch/spinlock.h` 用 `LDXR/STXR` 手写 fetch-and-increment，刻意不用 LSE：**

- 构建没有开 `-march=armv8.1-a`；一旦对普通 `u32` 用 `__atomic_*`，GCC 会降级成 **outline atomics**
  （调用 `__aarch64_ldadd4_acq_rel` 之类的 libgcc helper），freestanding 链接拿不到这些符号。
  自己写内联汇编是唯一不引入运行时依赖的做法。
- 取票（`next++`）用 **relaxed** 的 `ldxr/stxr` 就够了：票据本身不携带数据，真正的 publish 发生在
  后面 `while (__atomic_load_n(&lock->owner, __ATOMIC_ACQUIRE) != my)`——这条 acquire load 才是临界区入口。
  这一层顺序是 ticket lock 的经典论证，抄的时候容易误以为取票也要 acquire。
- 解锁只有 owner 写 `owner`，所以是 `ldr`+`__atomic_store_n(..., RELEASE)`，不需要 RMW。
- 自旋用 `yield` 而非 `WFE/SEV`：≤4 核、临界区极小；`WFE` 需要配对 `SEV` 并处理 event register 的竞态，
  在核数这么少时是纯风险。
- 锁**不屏蔽中断**；调用者必须已经在 mask 状态（启动期或异常入口），持锁时不得解除屏蔽。
  已知限制见 `hypervisor/include/spinlock.h` 头注释。

**CAS 本身当前没有出现在 EL2 代码里**：没有任何"比较后交换"的需求。客机里的 CAS（Linux 的
atomic ops、futex）编译成 EL1 的 LSE/独占指令，由硬件直接执行，EL2 不模拟也不 trap。

**对照实现**：`../bao-hypervisor/src/arch/armv8/aarch64/inc/arch/spinlock.h`（同类 ticket lock，
`ldaxr`+`stxr`，解锁 `stlr`+`dsb ish`+`sev` 供 `wfe` 唤醒）；`../xvisor/arch/arm/cpu/arm64/cpu_locks.c`。

## 坑清单

1. **跨 cache line 的独占访问**：`LDXR/STXR` 对跨 line 的访问可能永远失败（架构上允许）；16 字节
   `LDXP/STXP`/`CASP` 必须 16 字节对齐。QEMU 宽容，RK3588 不宽容。
2. **忘记 `CLREX`**：失败的 `STXR` 之后如果不再尝试，残留 monitor 会让后续无关的 exclusive 序列
   行为异常。编译器生成的 `__atomic_*` 会自动插，手写汇编不会。
3. **循环体里塞了副作用**：`printk`、`memset`、另一次 `LDXR` 都会毁掉重试循环的正确性（`printk`
   里还有自己的锁 → 自死锁）。
4. **store-store 重排**：`flag = 1; data = x;` 在弱序下可以反过来。先写数据、`DMB ISH`/release store
   再写 flag。
5. **`STLR` 当 RMW 用**：`ldr` + `add` + `stlr` 不是原子递增，多核下会丢更新。
6. **LSE 的 8 字节 `CAS` 对齐**：未对齐会触发 alignment fault，而不是"退化成慢路径"。

## 出处

- ARM ARM (DDI0487, A-profile)：B2.9.5 exclusive monitors、B2.10 single-copy atomicity、
  C6.2 各条 `CAS*`/`LDXR`/`STXR` 指令页、附录 K11/K13 barrier 与 litmus 测试、spinlock 示例。
- 本仓库：`hypervisor/include/spinlock.h`、`hypervisor/arch/arm64/include/arch/spinlock.h`、
  `hypervisor/arch/arm64/vmexit/vpsci.c`（发布 + kick 的 `dmb`/`dsb` 组合）。
- 无对应 host test：这些是硬件语义，`make host-test` 验证不了；实机行为靠 QEMU 场景间接覆盖。
