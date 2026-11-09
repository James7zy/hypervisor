# 提交信息规范（指引）

完整规则在
[scripts/commit-message/commit-message.md](../../scripts/commit-message/commit-message.md)，
本文件只记录与 git 历史、Linear 相关的补充约定。

## 标题

`[领域][专项] 组件: 摘要`，领域取自规范里的领域表，摘要整行 ≤ 72 字符。
**本文件不再是格式定义处**，别在这里复制一份领域表。

## 正文与尾部标签

与标题之间空一行，按 72 列换行。讲为什么改、改了什么、影响了谁（哪些调用方 / VM / 场景）。
不复述 diff；不写"更新了 X"这类没有信息量的句子。有已知限制的临时方案，写明适用范围和
重新设计的触发条件（与 `shortcut:` 注释同义）。

每个标签各占一行，放在正文之后，按需出现，顺序 `Test:` → `Bug:` / `Fixed:`。

| 标签 | 用途 |
| --- | --- |
| `Test: <命令或场景>` | 怎么验证的，例如 `Test: make test`、`Test: make host-test（sched 用例）`。没跑就写没跑，**不能写"已通过"**（见 `CLAUDE.md` §验证）。 |
| `Fixed: <Linear issue>` | 本提交关闭的问题。 |
| `Bug: <Linear issue>` | 相关但不关闭的问题。 |

本仓库没有 Gerrit，因此不写 `Change-Id:` / `Reviewed-on:` / `Reviewed-by:`；
`Fixed:` / `Bug:` 指向 Linear issue，不是 Monorail 编号。

## 检查

```sh
./scripts/commit-message/install-git-hooks.sh          # 装本地 commit-msg 钩子
scripts/commit-message/check-commit-message <file>     # 手动检查一条
```

## 与历史提交的关系

改用 `[领域][专项]` 之前的提交是 `type(scope): subject` 写法；**不改写历史**，新提交按规范执行。
