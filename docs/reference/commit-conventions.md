# 提交信息规范（指引）

完整规则在
[scripts/commit-message/commit-message.md](../../scripts/commit-message/commit-message.md)，
本文件只记录与 git 历史、Linear 相关的补充约定。

## 标题

`[领域][专项] 组件: 摘要`，领域取自规范里的领域表，摘要整行 ≤ 72 字符。
**本文件不再是格式定义处**，别在这里复制一份领域表。

## 正文

与标题之间空一行，按 72 列换行。讲为什么改、改了什么、影响了谁（哪些调用方 / VM / 场景）。
不复述 diff；不写"更新了 X"这类没有信息量的句子。有已知限制的临时方案，写明适用范围和
重新设计的触发条件（与 `shortcut:` 注释同义）。

**不写验证/追踪类尾部标签。** `Test:` / `Bug:` / `Fixed:` / `Change-Id:` /
`Reviewed-on:` / `Reviewed-by:` 一律不写；`Co-Authored-By:` 归属行可以保留。

验证证据不进 git 历史，进 Linear：切片结束那一条评论写清跑了什么、结果如何、报告在哪
（见 `CLAUDE.md` §验证、§Each unit of work ends with a Linear sync）。没实际跑的检查
不能写成"已通过"，这条约束不随 `Test:` 标签一起消失。

## 检查

```sh
./scripts/commit-message/install-git-hooks.sh          # 装本地 commit-msg 钩子
scripts/commit-message/check-commit-message <file>     # 手动检查一条
```

## 与历史提交的关系

改用 `[领域][专项]` 之前的提交是 `type(scope): subject` 写法；这些历史提交留在 `master` 上，
不再改写。`dev` 线 2026-11-26 一次性剥除了全部尾部标签，**该日期之前引用的 commit hash
已失效**。
