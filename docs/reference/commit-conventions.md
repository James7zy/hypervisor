# 提交信息规范 / Commit Conventions

本仓库的 commit message 规则，参考
[Fuchsia commit message style](https://fuchsia.dev/fuchsia-src/contribute/contribute-code/commit-messages)
并按本工程调整。**标题沿用本仓库已有的 `type(scope): subject` 写法**（`git log` 历史一直是这个格式），
正文与尾部标签采用 Fuchsia 的规矩。

本仓库没有 Gerrit，因此**不写** `Change-Id:`、`Reviewed-on:`、`Reviewed-by:`；
`Fixed:` / `Bug:` 指向 Linear issue 而不是 Monorail 编号。

## 结构

```
fix(sched): track the loaded resident even when LOAD is swept

LOAD sweep 之后 resident 仍指向旧 vCPU，下一次 switch-in 会把两个 vCPU
当成同一个处理，导致 survivor 拿不到 tick。把 resident 的更新挪到
sweep 的同一条路径上。

Test: make test（4 项静态检查 + 10 个 QEMU 场景全绿）
Fixed: hypervisor-<issue-id>
```

三段：标题、空行、正文（可选尾部标签）。

## 标题

- 格式 `type(scope): subject`，整行 ≤ 72 字符。
- `type` 取 `fix` / `test` / `docs` / `build` / `refactor` / `feat`。
- `scope` 取模块或里程碑：`sched`、`vgic`、`vcpu`、`arch`、`stage2`、`m11-c`……
- 祈使句，句尾不加句号；一眼能看出改了什么，不写“misc fixes”“update stuff”。

## 正文

- 与标题之间空一行，按 72 列换行，段落之间空行。
- 讲**为什么改**、改了什么、影响了谁（哪些调用方 / VM / 场景）。
- 不复述 diff；不写“更新了 X”这类没有信息量的句子。
- 有已知限制的临时方案，正文里写明适用范围和重新设计的触发条件（与 `shortcut:` 注释同义）。

## 尾部标签

每个各占一行，放在正文之后，按需出现，顺序 `Test:` → `Bug:` / `Fixed:`。

| 标签 | 用途 |
| --- | --- |
| `Test: <命令或场景>` | 怎么验证的，例如 `Test: make test`、`Test: make host-test（sched 用例）`。没跑就写没跑，**不能写“已通过”**（见 `CLAUDE.md` §验证）。 |
| `Fixed: <Linear issue>` | 本提交关闭的问题。 |
| `Bug: <Linear issue>` | 相关但不关闭的问题。 |

## 回滚 / 重提

```
Revert "fix(sched): track the loaded resident even when LOAD is swept"

This reverts commit <hash>.

Reason for revert: <为什么坏、观察到什么现象>
```

重新提交用 `Reland "<原标题>"`，正文说明上次失败的原因和这次的差别。

## 与历史提交的关系

M10 之前的提交大多只有一行标题、没有正文和标签；**不改写历史**，新提交按本文执行。
